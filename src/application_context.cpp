#include <cstdlib>
#include <chrono>
#include <iostream>
#include <thread>
#include "application_context.hpp"
#include "mjpeg_server.hpp"
#include "usb_capture_engine.hpp"

ApplicationContext::ApplicationContext(MjpegServer& server,
                                       UsbCaptureEngine& captureEngine,
                                       std::atomic<bool>& running)
    : server_(server), 
      captureEngine_(captureEngine),
      running_(running) {}

int ApplicationContext::run(const DeviceInfo& target) {
    auto& serverRef = server_.get();
    auto& captureEngineRef = captureEngine_.get();
    auto& runningRef = running_.get();

    std::cout << "[Server Core] Starting asynchronous capture and network worker engines...\n";

    captureEngineRef.start(target);
    serverRef.start();

    std::cout << "[Server Core] System fully operational. Awaiting network events.\n";
    while (runningRef.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "[Server Core] Shutdown signal received. Stopping worker lanes...\n";

    captureEngineRef.stop();
    
    return EXIT_SUCCESS;
}

void ApplicationContext::stop() {
    running_.get().store(false, std::memory_order_seq_cst);
}
