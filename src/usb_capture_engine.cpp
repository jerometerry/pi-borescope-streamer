#include <libusb.h>
#include <sys/time.h>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <span>
#include <thread>
#include <utility>
#include <vector> 
#include "server_constants.hpp"
#include "shared_frame_pipeline.hpp"
#include "usb_camera.hpp"
#include "usb_capture_engine.hpp"
#include "mjpeg_frame_decoder.hpp"

UsbCaptureEngine::UsbCaptureEngine(
    std::function<void(std::span<const uint8_t>)> dataSink, 
    std::atomic<bool>& running)
        : dataSink_(std::move(dataSink)), running_(running) {}

UsbCaptureEngine::~UsbCaptureEngine() { stop(); }

void UsbCaptureEngine::start(const DeviceInfo& target) {
    workerThread_ = std::thread(&UsbCaptureEngine::loop, this, target);
}

void UsbCaptureEngine::stop() {
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

void LIBUSB_CALL UsbCaptureEngine::transferCallback(struct libusb_transfer* transfer) {
    auto* engine = static_cast<UsbCaptureEngine*>(transfer->user_data);
    engine->handleIncomingTransfer(transfer);
}

void UsbCaptureEngine::handleIncomingTransfer(struct libusb_transfer* transfer) {
    if (transfer->status == LIBUSB_TRANSFER_COMPLETED && transfer->actual_length > 0) {
        std::span<const uint8_t> payloadView{
            transfer->buffer, 
            static_cast<size_t>(transfer->actual_length)
        };
        if (dataSink_) {
            dataSink_(payloadView);
        }
    } else if (transfer->status == LIBUSB_TRANSFER_NO_DEVICE) {
        running_ = false;
        return;
    }

    if (running_) {
        libusb_submit_transfer(transfer);
    }
}

void UsbCaptureEngine::loop(const DeviceInfo& target) {
    try {
        camera_ = std::make_unique<UsbCamera>(target);

        transferBuffers_.assign(ServerConstants::POOL_SIZE, std::vector<uint8_t>(ServerConstants::CHUNK_SIZE));

        for (int i = 0; i < ServerConstants::POOL_SIZE; ++i) {
            libusb_transfer* transfer = libusb_alloc_transfer(0);
            
            libusb_fill_bulk_transfer(
                transfer,
                camera_->getRawHandle(),
                1 | LIBUSB_ENDPOINT_IN,
                transferBuffers_[i].data(),
                ServerConstants::CHUNK_SIZE,
                transferCallback,
                this,
                ServerConstants::USB_TIMEOUT
            );

            libusb_submit_transfer(transfer);
            transferPool_.push_back(transfer);
        }

        while (running_) {
            int error = libusb_handle_events(camera_->getContext());
            if (error != LIBUSB_SUCCESS) {
                break;
            }
        }

        for (auto* transfer : transferPool_) {
            libusb_cancel_transfer(transfer);
        }

        struct timeval tv = {0, ServerConstants::ONE_HUNDRED_MILLISECONDS}; // 100ms timeout
        for (int i = 0; i < ServerConstants::POOL_SIZE; ++i) {
            libusb_handle_events_timeout(camera_->getContext(), &tv);
        }

        for (auto* transfer : transferPool_) {
            libusb_free_transfer(transfer);
        }
        transferPool_.clear();
        transferBuffers_.clear();

    } catch (...) {
        running_ = false;
    }
}