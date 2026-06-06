#pragma once
#include <libusb.h>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <span>
#include "constants.hpp"
#include "data_structures.hpp"
#include "device_info.hpp"
#include "usb_camera.hpp"

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
    std::atomic<int> activeTransfers_{0};
    std::unique_ptr<UsbCamera> camera_;
    std::thread workerThread_;
    std::vector<libusb_transfer*> transferPool_;

    void loop(const DeviceInfo& target) {
        try {
            camera_ = std::make_unique<UsbCamera>(target);

            uint8_t* dmaBuffer = libusb_dev_mem_alloc(
                camera_->getRawHandle(), 
                UsbConfig::DMA_BUFFER_SIZE
            );

            if (!dmaBuffer) {
                std::cerr << "[DRIVER ERROR] Failed to initialize Direct Access Memory \n";
                if (running_) {
                    running_->store(false, std::memory_order_release);
                }
                return;
            }

            for (size_t i = 0; i < UsbConfig::BULK_TRANSFER_COUNT; ++i) {
                libusb_transfer* transfer = libusb_alloc_transfer(0);
                libusb_fill_bulk_transfer(
                    transfer,
                    camera_->getRawHandle(),
                    1 | LIBUSB_ENDPOINT_IN,
                    dmaBuffer + (i * UsbConfig::BULK_TRANSFER_SIZE),
                    UsbConfig::BULK_TRANSFER_SIZE,
                    transferCallback,
                    this,
                    UsbConfig::USB_TIMEOUT
                );
                libusb_submit_transfer(transfer);
                activeTransfers_.fetch_add(1, std::memory_order_relaxed);
                transferPool_.push_back(transfer);
            }

            struct timeval activeTimeValue = {0, Units::ONE_HUNDRED_MILLISECONDS};
            while (running_->load(std::memory_order_relaxed)) {
                int error = libusb_handle_events_timeout(
                    camera_->getContext(), 
                    &activeTimeValue
                );
                if (error != LIBUSB_SUCCESS) {
                    std::cerr << std::format("libusb_handle_events failed. Error: {}\n", error);
                    break;
                }
            }

            for (auto* transfer : transferPool_) {
                libusb_cancel_transfer(transfer);
            }

            struct timeval shutdownTimeValue = {0, UsbConfig::SHUTDOWN_WAIT_TIMEOUT}; 
            while (activeTransfers_.load(std::memory_order_acquire) > 0) {
                libusb_handle_events_timeout(
                    camera_->getContext(), 
                    &shutdownTimeValue
                );
            }

            for (auto* transfer : transferPool_) {
                libusb_free_transfer(transfer);
            }
            transferPool_.clear();

            int freeResult = libusb_dev_mem_free(
                camera_->getRawHandle(), 
                dmaBuffer, 
                UsbConfig::DMA_BUFFER_SIZE
            );
            if (freeResult != LIBUSB_SUCCESS) {
                std::cerr << std::format("[DRIVER ERROR] Failed to free DMA: {} \n", freeResult);
            }

        } catch (const std::exception& e) {
            std::cerr << "[DRIVER ERROR] Terminated via standard exception: " << e.what() << '\n';
            if (running_) {
                running_->store(false, std::memory_order_release);
            }
        } catch (...) {
            std::cerr << "[DRIVER ERROR] Terminated via completely unhandled exception pattern!\n";
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

        bool shouldResubmit = false;

        if (transfer->status != LIBUSB_TRANSFER_CANCELLED) {
            shouldResubmit = driver->transferHandler_(status, payload);
        }

        if (!shouldResubmit || transfer->status == LIBUSB_TRANSFER_CANCELLED) {
            driver->running_->store(false, std::memory_order_release);
            driver->activeTransfers_.fetch_sub(1, std::memory_order_release);
        } else if (driver->running_->load(std::memory_order_relaxed)) {
            if (libusb_submit_transfer(transfer) != LIBUSB_SUCCESS) {
                driver->activeTransfers_.fetch_sub(1, std::memory_order_release);
            }
        } else {
            driver->activeTransfers_.fetch_sub(1, std::memory_order_release);
        }
    }
};