#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib> 
#include <functional>
#include <iostream>
#include <span>
#include <string>
#include <memory>
#include <thread>
#include <vector>
#include "buffer.hpp"
#include "buffer_pool.hpp"
#include "buffer_ptr.hpp"
#include "constants.hpp"
#include "intrusive_ptr.hpp"
#include "mjpeg_frame_queue.hpp"
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
    // Disable stdout buffering so logs immediately appear in systemd journalctl
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

    auto pool = BufferPool::create();

    // MjpegFrameQueue HAS to be initialized after BufferPool!
    // Prevents segfaults if MjpegFrameQueue goes out of scope before program terminates. 
    MjpegFrameQueue queue;
    MjpegStream stream(pool, [&queue](const BufferPtr& frame) {
        queue.push(frame);
    });

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

    while (running.load(std::memory_order_relaxed)) {
        uint32_t currentFrameId = 0;
        auto currentFrame = queue.pop(currentFrameId);

        if (currentFrame && !currentFrame->empty() && currentFrameId != lastBroadcastedFrameId) {
            publisher.writeFrame(currentFrame);
            lastBroadcastedFrameId = currentFrameId;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    driver.stop();
    
    std::cout << "[System Termination] V4L2 daemon exited cleanly.\n";
    return EXIT_SUCCESS;
}
