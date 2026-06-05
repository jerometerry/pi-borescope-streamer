#pragma once
#include <libusb.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>
#include <span>
#include "device_info.hpp"
#include "usb_camera.hpp"
#include "server_constants.hpp"

namespace USB {
    enum class TransferStatus :std::uint8_t {
        Completed,
        Disconnected,
        Error
    };
}

/**
 * @brief A zero-cost template wrapper that manages the libusb event loop.
 * @tparam FrameProcessor A class implementing `bool processTransfer(USB::TransferStatus, std::span<const uint8_t>)`
 */
template <typename Callable>
class LibusbAsyncDriver {
public:
    LibusbAsyncDriver(Callable transferHandler, std::atomic<bool>* running)
        : transferHandler_(std::move(transferHandler)), running_(running) {}

    ~LibusbAsyncDriver() { stop(); }

    void start(const DeviceInfo& target) {
        workerThread_ = std::thread(&LibusbAsyncDriver::loop, this, target);
    }

    void stop() {
        if (workerThread_.joinable()) {
            workerThread_.join();
        }
    }

private:
    Callable transferHandler_;
    std::atomic<bool>* running_;
    std::unique_ptr<UsbCamera> camera_;
    std::thread workerThread_;
    std::vector<libusb_transfer*> transferPool_;
    std::vector<std::vector<uint8_t>> transferBuffers_;

    void loop(const DeviceInfo& target) {
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

            
            struct timeval tvRunning = {0, 0};
            while (running_->load(std::memory_order_relaxed)) {
                int error = libusb_handle_events_timeout_completed(
                    camera_->getContext(), &tvRunning, nullptr);
                if (error != LIBUSB_SUCCESS) { 
                    break;
                }
                std::this_thread::yield();
            }

            for (auto* transfer : transferPool_) {
                libusb_cancel_transfer(transfer);
            }

            struct timeval tv = {0, ServerConstants::ONE_HUNDRED_MILLISECONDS};
            for (int i = 0; i < ServerConstants::POOL_SIZE; ++i) {
                libusb_handle_events_timeout(camera_->getContext(), &tv);
            }

            for (auto* transfer : transferPool_) {
                libusb_free_transfer(transfer);
            }
            transferPool_.clear();
            transferBuffers_.clear();

        } catch (...) {
            if (running_) {
                running_->store(false, std::memory_order_release);
            }
        }
    }

    static void LIBUSB_CALL transferCallback(struct libusb_transfer* transfer) {
        auto* driver = static_cast<LibusbAsyncDriver*>(transfer->user_data);
        
        USB::TransferStatus status = USB::TransferStatus::Error;
        if (transfer->status == LIBUSB_TRANSFER_COMPLETED) {
            status = USB::TransferStatus::Completed;
        } else if (transfer->status == LIBUSB_TRANSFER_NO_DEVICE) {
            status = USB::TransferStatus::Disconnected;
        }

        std::span<const uint8_t> payload;
        if (status == USB::TransferStatus::Completed && transfer->actual_length > 0) {
            payload = std::span<const uint8_t>(transfer->buffer, transfer->actual_length);
        }

        bool shouldResubmit = driver->transferHandler_(status, payload);

        if (!shouldResubmit) {
            driver->running_->store(false, std::memory_order_release);
        } else if (driver->running_->load(std::memory_order_relaxed)) {
            libusb_submit_transfer(transfer);
        }
    }
};