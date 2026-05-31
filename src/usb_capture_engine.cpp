#include <libusb.h>
#include <atomic>
#include <memory>
#include <span>
#include <thread>
#include "device_info.hpp"
#include "hardware_button_manager.hpp"
#include "shared_frame_pipeline.hpp"
#include "usb_camera.hpp"
#include "usb_capture_engine.hpp"
#include "usb_frame_decoder.hpp"

UsbCaptureEngine::UsbCaptureEngine(SharedFramePipeline& pipeline, HardwareButtonManager& buttonManager, std::atomic<bool>& running)
        : pipeline_(pipeline), buttonManager_(buttonManager), running_(running) {}

UsbCaptureEngine::~UsbCaptureEngine() { stop(); }

void UsbCaptureEngine::start(const DeviceInfo& target) {
    workerThread_ = std::thread(&UsbCaptureEngine::loop, this, target);
}

void UsbCaptureEngine::stop() {
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

void UsbCaptureEngine::loop(const DeviceInfo& target) {
    try {
        camera_ = std::make_unique<UsbCamera>(target);
        
        decoder_ = std::make_unique<UsbFrameDecoder>(
            [this](const std::vector<uint8_t>& frame) { pipeline_.updateFrame(frame); },
            [this]() { buttonManager_.registerHardwarePress(); }
        );

        std::vector<uint8_t> readBuffer;
        readBuffer.reserve(4096); // 4KB aligned

        while (running_) {
            int error = camera_->read(readBuffer);
            if (error == 0) {
                decoder_->processIncomingCameraData(std::span<const uint8_t>{readBuffer});
            } else if (error == LIBUSB_ERROR_NO_DEVICE) {
                running_ = false;
            }
            
            if (buttonManager_.checkAndResetQuickPressTrigger()) {
                pipeline_.requestSnapshot();
            }
        }
    } catch (...) {
        running_ = false;
    }
}

    
