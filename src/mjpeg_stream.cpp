#include <cstdlib>
#include <chrono>
#include <iostream>
#include <thread>
#include "mjpeg_stream.hpp"
#include "usb_capture_engine.hpp"
#include "web_server.hpp"

MjpegStream::MjpegStream(SharedFramePipeline& pipeline, 
                         HardwareButtonManager& buttonManager, 
                         WebServer& server,
                         std::atomic<bool>& running)
    : pipeline_(pipeline), 
      buttonManager_(buttonManager), 
      server_(server), 
      running_(running) {}

int MjpegStream::run(const DeviceInfo& target) {
    auto& pipelineRef = pipeline_.get();
    auto& buttonManagerRef = buttonManager_.get();
    auto& serverRef = server_.get();
    auto& runningRef = running_.get();

    UsbCaptureEngine captureEngine(pipelineRef, buttonManagerRef, runningRef);

    std::cout << "[Server Core] Starting asynchronous capture and network worker engines...\n";
    captureEngine.start(target);
    serverRef.start();

    std::cout << "[Server Core] System fully operational. Awaiting network events.\n";
    while (runningRef.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "[Server Core] Shutdown signal received. Stopping worker lanes...\n";
    captureEngine.stop();
    
    return EXIT_SUCCESS;
}

void MjpegStream::stop() {
    running_.get().store(false, std::memory_order_seq_cst);
}
