#pragma once
#include <thread>
#include <atomic>
#include <memory>
#include <span>
#include <libusb.h>
#include "usb_camera.hpp"
#include "usb_frame_decoder.hpp"
#include "shared_frame_pipeline.hpp"
#include "hardware_button_manager.hpp"

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
