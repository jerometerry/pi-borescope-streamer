#pragma once
#include <atomic>
#include <memory>
#include <thread>
class HardwareButtonManager;
class SharedFramePipeline;
class UsbCamera;
class UsbFrameDecoder;
struct DeviceInfo;

class UsbCaptureEngine {
public:
    UsbCaptureEngine(SharedFramePipeline& pipeline, HardwareButtonManager& buttonManager, std::atomic<bool>& running);
    ~UsbCaptureEngine();

    void start(const DeviceInfo& target);

    void stop();

private:
    std::unique_ptr<UsbCamera> camera_;
    std::unique_ptr<UsbFrameDecoder> decoder_;
    SharedFramePipeline& pipeline_;
    HardwareButtonManager& buttonManager_;
    std::atomic<bool>& running_;
    std::thread workerThread_;

    void loop(const DeviceInfo& target);
};
