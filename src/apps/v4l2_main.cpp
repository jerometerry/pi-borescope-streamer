#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib> 
#include <iostream>
#include <span>
#include <string>
#include <thread>
#include <vector>
#include "constants.hpp"
#include "frame.hpp"
#include "frame_disruptor.hpp"
#include "mjpeg_stream.hpp"
#include "usb_device_info.hpp"
#include "usb_device_finder.hpp"
#include "usb_driver.hpp"
#include "v4l2.hpp"
#include "v4l2_publisher.hpp"

namespace {
    std::atomic<bool> running{true};
}

void signalHandler(int signal) {
    std::cout << "\n[System] Signal " << signal << " received. Shutting down V4L2 daemon...\n";
    running.store(false, std::memory_order_release);
}

int main(int argc, const char* argv[]) {
    std::cout << std::unitbuf;

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    V4L2::Config config;
    
    Arguments::ParseResult result = V4L2::parseArguments(argc, argv, config);
    if (result == Arguments::ParseResult::HelpRequested) {
        return EXIT_SUCCESS;
    } else if (result == Arguments::ParseResult::Error) {
        return EXIT_FAILURE;
    }

    std::cout << "=================================================\n"
              << "[Startup] Initializing V4L2 Borescope Daemon\n"
              << "  -> Device Path : " << config.devicePath << "\n"
              << "  -> Bus         : " << config.bus << "\n"
              << "  -> Address     : " << config.address << "\n"
              << "  -> Resolution  : " << config.width << "x" << config.height << "\n"
              << "  -> Max Buffer  : " << config.sizeImage << " bytes\n"
              << "=================================================\n";

    auto cameras = UsbDeviceFinder::superCameras();
    if (cameras.empty()) {
        std::cerr << "[Fatal] No compatible Useeplus cameras found on the USB bus.\n";
        return EXIT_FAILURE;
    }

    const UsbDeviceInfo& camera = cameras[0];
    std::cout << "[Info] Binding to camera on Bus " << static_cast<int>(camera.bus) 
              << " Address " << static_cast<int>(camera.address) << "...\n";

    FrameDisruptor ringBuffer;
    ringBuffer.preAllocate(Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);

    MjpegStream stream(ringBuffer);

    auto transfer = [&stream](UsbTransferStatus status, std::span<const uint8_t> payload) -> bool {
        if (status == UsbTransferStatus::Completed) {
            if (!payload.empty()) {
                stream.send(payload);
            }
            return true;
        }
        return status != UsbTransferStatus::Disconnected; 
    };

    UsbDriver driver(transfer, &running);
    driver.start(camera);

    V4l2Publisher publisher(config);

    std::cout << "[V4L2 Core] Streaming daemon active and routing frames.\n";

    uint32_t lastBroadcastedFrameId = 0;

    int64_t next_read = 0;
    uint32_t currentFrameId = static_cast<uint32_t>(next_read);
    while (running.load(std::memory_order_relaxed)) {
        int64_t available = ringBuffer.waitFor(next_read);

        while (next_read <= available) {
            Frame& slot = ringBuffer.getBySequence(next_read);

            if (slot.active_size > 0) {
                if (currentFrameId != lastBroadcastedFrameId) {
                    publisher.writeFrame(slot);
                    lastBroadcastedFrameId = currentFrameId;
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }

            next_read++;
        }
        ringBuffer.markConsumed(next_read - 1);
    }
    ringBuffer.markConsumed(next_read - 1);

    driver.stop();

    int64_t seq = ringBuffer.claim();
    Frame& slot = ringBuffer.getBySequence(seq);
    slot.clear();
    ringBuffer.publish(seq);
    
    std::cout << "[System Termination] V4L2 daemon exited cleanly.\n";
    return EXIT_SUCCESS;
}
