#include "mjpeg_stream.hpp"
#include "usb_camera.hpp"
#include "usb_frame_decoder.hpp"
#include "web_server.hpp"
#include "server_constants.hpp"
#include "device_info.hpp"

#include <atomic>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <span>
#include <string>
#include <mutex>
#include <thread>
#include <vector>
//#include <fstream>
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
    auto currentTime = serverTime.now();
    std::scoped_lock<std::mutex> lock(buttonMutex);

    auto elapsedMs = serverTime.getElapsedMilliseconds(buttonLastSeen, currentTime);

    // If the button is currently not detected as pressed, or it has been detected as pressed but we haven't seen it still pressed for at least the debounce time, we can treat this as a new button press event. We then update the button press start time and set the depressed state to true. If the button is already depressed and we've seen it still pressed within the debounce time, we just update the last seen time to keep tracking how long it's been held down. This allows us to filter out noise and chatter from the button and only respond to legitimate presses.
    if (!buttonIsDepressed || elapsedMs > BUTTON_DEBOUNCE_TIME_MS) {
        buttonPressStart = currentTime;
        buttonIsDepressed = true;
    }
    buttonLastSeen = currentTime;
}

void MjpegStream::checkForButtonQuickPress() {
    auto currentTime = serverTime.now();
    std::scoped_lock<std::mutex> lock(buttonMutex);

    if (buttonIsDepressed) {
        auto elapsedMs = serverTime.getElapsedMilliseconds(buttonLastSeen, currentTime);

        // If the button was previously detected as pressed but hasn't been seen as still pressed for a certain amount of time, we can infer that it was released. We then check how long the button was held down to determine if it qualifies as a "quick press" for snapshot capture or a long press for lens toggle (which we ignore, since a long press is a hardware event that toggles the lens).
        if (elapsedMs > QUICK_PRESS_MIN_MS) {
            auto durationMs = serverTime.getElapsedMilliseconds(
                buttonPressStart, buttonLastSeen);
            buttonIsDepressed = false;

            if (durationMs < QUICK_PRESS_MAX_MS) {
                std::cout << "[Button Filter] Quick press matching window (" << durationMs << "ms). Capturing...\n";
                snapshotNextFrame = true;
            } else {
                std::cout << "[Button Filter] Long press lens toggle bypass (" << durationMs << "ms).\n";
            }
        }
    }
}

void MjpegStream::startVideoFeed(const DeviceInfo& target) {
    auto broadcastHandler = [this](const std::vector<uint8_t>& frame) { 
        broadcastFrame(frame);
    };
    auto buttonHandler = [this]() { 
        hardwareButtonCallback(); 
    };

    try {
        UsbCamera camera(target);
        UsbFrameDecoder decoder(broadcastHandler, buttonHandler);
        std::cout << "[Hardware Engine] Pipeline operational.\n";

        // --- BINARY FILE LOGGER ---
        //std::cout << "[Debug] Opening binary stream dump: raw_camera_dump.bin\n";
        //std::ofstream rawDump("raw_camera_dump.bin", std::ios::binary);

        // Main capture loop: continuously read frames from the camera and pass them to the protocol handler for processing. If the camera is disconnected, libusb will return an error code which we check for to break the loop and initiate shutdown. We also call the button release state monitor on each iteration to check for any button release events that may have occurred since the last frame read.
        std::vector<uint8_t> readBuffer;
        readBuffer.reserve(ServerConstants::ONE_MEGABYTE);

        while (running) {
            int error = camera.read(readBuffer);
            if (error == 0) {

                //if (rawDump.is_open()) {
                //    rawDump.write(reinterpret_cast<const char*>(readBuffer.data()), readBuffer.size());
                //    rawDump.flush(); // Ensure data survives a crash/Ctrl+C
                //}

                decoder.processIncomingCameraData(std::span<const uint8_t>{readBuffer});
            } else if (error == LIBUSB_ERROR_NO_DEVICE) {
                std::cerr << "[Hardware Engine] Device disconnected.\n";
                running = false;
            }
            checkForButtonQuickPress();
        }
    } catch (const std::exception &exception) {
        std::cerr << "[Hardware Engine Exception]: " << exception.what() << "\n";
        running = false;
    }
}