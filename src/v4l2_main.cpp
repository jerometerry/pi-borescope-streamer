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
#include "argument_parser.hpp"
#include "device_info.hpp"
#include "device_finder.hpp"
#include "frame_exchange.hpp"
#include "libusb_async_driver.hpp"
#include "mjpeg_frame_decoder.hpp"
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

    auto cameras = DeviceFinder::superCameras();
    if (cameras.empty()) {
        std::cerr << "[Fatal] No compatible Useeplus cameras found on the USB bus.\n";
        return EXIT_FAILURE;
    }
    
    const DeviceInfo& camera = cameras[0];
    std::cout << "[Info] Binding to camera on Bus " << static_cast<int>(camera.bus) 
              << " Address " << static_cast<int>(camera.address) << "...\n";

    FrameExchange exchange;

   MjpegFrameDecoder decoder([&exchange](const std::vector<uint8_t>& frame) {
        exchange.publishFrame(frame);
    });

    auto usbRouter = [&decoder](USB::TransferStatus status, std::span<const uint8_t> payload) -> bool {
        if (status == USB::TransferStatus::Completed) {
            if (!payload.empty()) {
                decoder.processIncomingCameraData(payload);
            }
            return true;
        }
        return status != USB::TransferStatus::Disconnected; 
    };

    LibusbAsyncDriver<decltype(usbRouter)> usbDriver(usbRouter, &running);
    usbDriver.start(camera);

    V4l2Publisher publisher(config);

    std::cout << "[V4L2 Core] Streaming daemon active and routing frames.\n";

    uint32_t lastBroadcastedFrameId = 0;

    while (running.load(std::memory_order_relaxed)) {
        uint32_t currentFrameId = 0;
        auto currentFrame = exchange.getLatestFrame(currentFrameId);

        if (currentFrame && !currentFrame->empty() && currentFrameId != lastBroadcastedFrameId) {
            publisher.writeFrame(*currentFrame);
            lastBroadcastedFrameId = currentFrameId;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    usbDriver.stop();
    
    std::cout << "[System Termination] V4L2 daemon exited cleanly.\n";
    return EXIT_SUCCESS;
}