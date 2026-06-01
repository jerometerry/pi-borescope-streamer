#pragma once

#include <atomic>
#include <functional>
#include "usb_capture_engine.hpp"

class SharedFramePipeline;
class HardwareButtonManager;
class WebServer;
struct DeviceInfo;

class ApplicationContext {
public:
    ApplicationContext(SharedFramePipeline& pipeline, 
                HardwareButtonManager& buttonManager, 
                WebServer& server,
                std::atomic<bool>& running);
    ~ApplicationContext() = default;

    ApplicationContext(const ApplicationContext&) = delete;
    ApplicationContext& operator=(const ApplicationContext&) = delete;

    int run(const DeviceInfo& target);
    void stop();

private:
    std::reference_wrapper<SharedFramePipeline> pipeline_;
    std::reference_wrapper<HardwareButtonManager> buttonManager_;
    std::reference_wrapper<WebServer> server_;
    std::reference_wrapper<std::atomic<bool>> running_;

    UsbCaptureEngine captureEngine_;
};
