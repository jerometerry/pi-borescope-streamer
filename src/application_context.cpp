#include <cstdlib>
#include <chrono>
#include <iostream>
#include <thread>
#include "application_context.hpp"
#include "usb_capture_engine.hpp"
#include "web_server.hpp"

ApplicationContext::ApplicationContext(SharedFramePipeline& pipeline, 
                         HardwareButtonManager& buttonManager, 
                         MjpegServer& server,
                         std::atomic<bool>& running)
    : pipeline_(pipeline), 
      buttonManager_(buttonManager), 
      server_(server), 
      running_(running),
      captureEngine_(pipeline, buttonManager, running) {} // 💡 Forward dependencies cleanly

int ApplicationContext::run(const DeviceInfo& target) {
    auto& serverRef = server_.get();
    auto& runningRef = running_.get();

    std::cout << "[Server Core] Starting asynchronous capture and network worker engines...\n";

    captureEngine_.start(target);
    serverRef.start();

    std::cout << "[Server Core] System fully operational. Awaiting network events.\n";
    while (runningRef.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "[Server Core] Shutdown signal received. Stopping worker lanes...\n";

    captureEngine_.stop();
    
    return EXIT_SUCCESS;
}

void ApplicationContext::stop() {
    running_.get().store(false, std::memory_order_seq_cst);
}
