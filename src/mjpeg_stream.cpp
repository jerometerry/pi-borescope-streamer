#include "mjpeg_stream.hpp"
#include "usb_camera.hpp"
#include "usb_context.hpp"
#include "usb_camera_protocol.hpp"
#include "web_server.hpp"
#include "device_finder.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <format>
#include <iostream>
#include <string>
#include <mutex>
#include <thread>
#include <vector>
#include <libusb.h>

MjpegStream::MjpegStream(const ServerTime& serverTime) : serverTime(serverTime) {}

MjpegStream::~MjpegStream() {}

int MjpegStream::run(int port, const DeviceInfo& target) {
    frameBuffer.reserve(ServerConstants::ONE_MEGABYTE);
    snapshotBuffer.reserve(ServerConstants::ONE_MEGABYTE);

    WebServer server(
        port, 
        running, 
        frameId,
        frameBuffer,
        frameMutex, 
        snapshotBuffer,
        snapshotMutex
    );

    if (!server.initialize()) {
        std::cerr << "Failed to initialize async web server.\n";
        return 1;
    }

    std::thread streamThread(&MjpegStream::startVideoFeed, this, target);

    server.start();

    if (streamThread.joinable()) {
        streamThread.join();
    }
    
    std::cout << "Application cleanly terminated.\n";
    return EXIT_SUCCESS;
}

void MjpegStream::stop() {
    running = false;
}

void MjpegStream::broadcastFrame(const std::vector<uint8_t>& frame) {
    // If the frame is empty, there's nothing to broadcast, so just return early. This can happen if the camera produces an empty frame for any reason, and we want to avoid broadcasting invalid data to clients.
    if (frame.empty()) {
        return;
    }

    {
        std::scoped_lock<std::mutex> lock(frameMutex);

        // Attempt to find the JPEG SOI markers within the first 32 bytes of the frame data.
        size_t offset = std::string::npos;
        for (size_t index = 0; index + 1 < frame.size() && index < JPEG_SOI_MARKERS_MAX_POSITION; ++index) {
            if (frame[index] == JPEG_SOI_MARKERS[0] && frame[index + 1] == JPEG_SOI_MARKERS[1]) {
                offset = index;
                break;
            }
        }

        // If the markers were found, discard any leading bytes before the SOI. This can help mitigate issues with certain cameras that prepend extraneous data before the JPEG frame. If the markers aren't found, just use the entire frame as-is.
        if (offset != std::string::npos) {
            frameBuffer.assign(frame.begin() + offset, frame.end());
        } else {
            frameBuffer = frame;
        }

        // Increment the frame ID to signal to any connected clients that a new frame is available. This happens after the frame buffer is updated to ensure clients can safely read the new frame data when they see the ID change.
        frameId++;

        // If a snapshot capture was requested by the hardware button, copy the latest frame to the snapshot buffer for serving to snapshot clients. This happens after the main frame buffer and ID are updated to ensure the snapshot captures the most recent frame.
        if (snapshotNextFrame) {
            std::scoped_lock<std::mutex> snapshotLock(snapshotMutex);
            snapshotBuffer = frameBuffer;
            snapshotNextFrame = false;
            std::cout << "[Server Core] Button Still-Frame Captured (" << snapshotBuffer.size() << " bytes)\n";
        }
    }
}

void MjpegStream::hardwareButtonCallback() {
    auto currentTime = std::chrono::steady_clock::now();
    std::scoped_lock<std::mutex> lock(buttonMutex);

    auto elapsed = currentTime - buttonLastSeen;
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    if (elapsedMs > BUTTON_DEBOUNCE_TIME_MS) {
        std::cout << "[Hardware Engine] Button pulse received. Capturing snapshot...\n";
        snapshotNextFrame = true;
    }

    buttonLastSeen = currentTime;
}

void MjpegStream::startVideoFeed(const DeviceInfo& target) {
    auto broadcastHandler = [this](const std::vector<uint8_t>& frame) { 
        broadcastFrame(frame);
    };
    auto buttonHandler = [this]() { 
        hardwareButtonCallback(); 
    };

    UsbCameraProtocol protocol(broadcastHandler, buttonHandler);

    DeviceInfo currentTarget = target;

    while (running) {
        try {
            UsbContext context;
            UsbCamera camera(currentTarget);

            libusb_device_handle* handle = DeviceFinder::open(context, currentTarget);

            if (!camera.open(handle)) {
                // Camera not found. Sleep for 1 second and check the bus again.
                std::this_thread::sleep_for(std::chrono::seconds(1));

                // Linux changes the USB address on replug. We must scan the bus 
                // and update our target with the new OS-assigned address.
                auto activeDevices = DeviceFinder::list(true);
                for (const auto& dev : activeDevices) {
                    if (dev.isSameDevice(target)) {
                        currentTarget = dev; 
                        break;
                    }
                }

                continue;
            }

            std::cout << std::format("{} [Hardware Engine] Pipeline operational...\n", serverTime.get());
            
            std::vector<uint8_t> readBuffer;
            readBuffer.reserve(ServerConstants::ONE_MEGABYTE);

            while (running) {
                int error = camera.readFrame(readBuffer);
                
                if (error == 0) {
                    protocol.handleFrame(readBuffer);
                } else if (error == LIBUSB_ERROR_NO_DEVICE) {
                    std::cerr << "[Hardware Engine] Device unplugged. Waiting for reconnection...\n";
                    break;
                }
            }
            
        } catch (const std::exception &exception) {
            std::cerr << "[Hardware Engine Exception]: " << exception.what() << "\n";
            std::cerr << "Retrying in 2 seconds...\n";
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }
}