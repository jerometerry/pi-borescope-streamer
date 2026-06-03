#include <stdint.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib> 
#include <exception>
#include <iostream>
#include <string>
#include <memory>
#include <thread>
#include <vector>
#include "device_info.hpp"
#include "device_finder.hpp"
#include "shared_frame_pipeline.hpp"
#include "usb_capture_engine.hpp"
#include "v4l2_publisher.hpp"
#include "v4l2_config.hpp"

std::atomic<bool> globalRunning{true};

void signalHandler(int signal) {
    std::cout << "\nSignal " << signal << " received. Shutting down V4L2 daemon...\n";
    globalRunning.store(false, std::memory_order_release);
}

enum class ParseResult {
    Success,
    HelpRequested,
    Error
};

ParseResult parseArguments(int argc, const char* argv[], V4l2Config& config) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        try {
            if (arg == "--dev" && i + 1 < argc) {
                config.devicePath = argv[++i];
            } else if (arg == "--width" && i + 1 < argc) {
                config.width = std::stoi(argv[++i]);
            } else if (arg == "--height" && i + 1 < argc) {
                config.height = std::stoi(argv[++i]);
            } else if (arg == "--size" && i + 1 < argc) {
                config.sizeImage = std::stoull(argv[++i]);
            } else if (arg == "--help") {
                std::cout << "Usage: v4l2-borescope-daemon [options]\n"
                          << "Options:\n"
                          << "  --dev <path>     Path to loopback device (default: /dev/video7)\n"
                          << "  --width <px>     Video width (default: 640)\n"
                          << "  --height <px>    Video height (default: 480)\n"
                          << "  --size <bytes>   Max frame buffer size (default: 131072)\n"
                          << "  --help           Show this message\n";
                return ParseResult::HelpRequested;
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                return ParseResult::Error;
            }
        } catch (const std::exception& e) {
            std::cerr << "Invalid value provided for argument " << arg << "\n";
            return ParseResult::Error;
        }
    }
    
    return ParseResult::Success;
}

int main(int argc, const char* argv[]) {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    V4l2Config config;

    ParseResult result = parseArguments(argc, argv, config);
    if (result == ParseResult::HelpRequested) {
        return EXIT_SUCCESS;
    } else if (result == ParseResult::Error) {
        return EXIT_FAILURE;
    }

    auto cameras = DeviceFinder::superCameras();
    if (cameras.empty()) {
        std::cerr << "[Fatal] No compatible Useeplus cameras found on the USB bus.\n";
        return EXIT_FAILURE;
    }
    
    const DeviceInfo camera = cameras[0];
    std::cout << "[Info] Binding to camera on Bus " << static_cast<int>(camera.bus) 
              << " Address " << static_cast<int>(camera.address) << "...\n";

    SharedFramePipeline pipeline;
    UsbCaptureEngine captureEngine(pipeline, globalRunning);
    V4l2Publisher publisher(config);

    captureEngine.start(camera);

    std::cout << "[V4L2 Core] Streaming daemon active. Press Ctrl+C to stop.\n";

    uint32_t lastBroadcastedFrameId = 0;

    while (globalRunning.load(std::memory_order_relaxed)) {
        uint32_t currentFrameId = 0;
        auto currentFrame = pipeline.getCurrentFrame(currentFrameId);

        if (currentFrame && !currentFrame->empty() && currentFrameId != lastBroadcastedFrameId) {
            publisher.writeFrame(*currentFrame);
            lastBroadcastedFrameId = currentFrameId;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    captureEngine.stop();
    std::cout << "[System Termination] V4L2 daemon exited cleanly.\n";
    return EXIT_SUCCESS;
}