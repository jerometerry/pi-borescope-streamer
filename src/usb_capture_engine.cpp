#include "usb_capture_engine.hpp"

UsbCaptureEngine::UsbCaptureEngine(std::function<void(std::span<const uint8_t>)> dataSink)
    : dataSink_(std::move(dataSink)) {}

bool UsbCaptureEngine::processTransfer(UsbTransferStatus status, std::span<const uint8_t> payload) {
    if (status == UsbTransferStatus::Completed) {
        if (!payload.empty() && dataSink_) {
            dataSink_(payload);
        }
        return true;
    } else if (status == UsbTransferStatus::Disconnected) {
        return false;
    }
    
    return true; 
}