#include <libusb.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <span>
#include "constants.hpp"
#include "usb_camera.hpp"
#include "usb_device_info.hpp"
#include "usb_driver.hpp"

UsbDriver::UsbDriver(TransferHandler transferHandler, std::atomic<bool>* running) : 
	transferHandler_(std::move(transferHandler)), running_(running) {}

UsbDriver::~UsbDriver() { 
	stop(); 
}

void UsbDriver::start(const UsbDeviceInfo& target) {
	workerThread_ = std::thread(&UsbDriver::loop, this, target);
}

void UsbDriver::stop() {
	if (workerThread_.joinable()) {
		workerThread_.join();
	}
}

void UsbDriver::loop(const UsbDeviceInfo& target) {
	try {
		camera_ = std::make_unique<UsbCamera>(target);

		bool isDmaAllocated = true;
		uint8_t* dmaBuffer = reinterpret_cast<uint8_t*>(libusb_dev_mem_alloc(
			camera_->getRawHandle(), 
			UsbConfig::DMA_BUFFER_SIZE
		));

		if (!dmaBuffer) {
			std::cout << "[DRIVER INFO] DMA allocation failed or unsupported. Falling back to vector.\n";
			isDmaAllocated = false;
			transferMemory_.resize(UsbConfig::DMA_BUFFER_SIZE);
			dmaBuffer = transferMemory_.data();
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
			
			int submitResult = libusb_submit_transfer(transfer);
			if (submitResult == LIBUSB_SUCCESS) {
				activeTransfers_.fetch_add(1, std::memory_order_relaxed);
				transferPool_.push_back(transfer);
			} else {
				std::cerr << std::format("[DRIVER ERROR] Failed to submit transfer: {}\n", submitResult);
				libusb_free_transfer(transfer);
			}
		}

		struct timeval activeTimeValue = {0, Units::ONE_HUNDRED_MILLISECONDS};
		while (running_->load(std::memory_order_relaxed)) {
			int error = libusb_handle_events_timeout(
				camera_->getContext(), 
				&activeTimeValue
			);
			// Ignore LIBUSB_ERROR_INTERRUPTED during shutdown signals
			if (error != LIBUSB_SUCCESS && error != LIBUSB_ERROR_INTERRUPTED) {
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

		for (int i = 0; i < 5; ++i) {
			struct timeval finalFlush = {0, 1000}; // 1 millisecond
			libusb_handle_events_timeout(camera_->getContext(), &finalFlush);
		}

		for (auto* transfer : transferPool_) {
			libusb_free_transfer(transfer);
		}
		transferPool_.clear();

		if (isDmaAllocated && dmaBuffer) {
			int freeResult = libusb_dev_mem_free(
				camera_->getRawHandle(), 
				dmaBuffer, 
				UsbConfig::DMA_BUFFER_SIZE
			);
			if (freeResult != LIBUSB_SUCCESS) {
				std::cerr << std::format("[DRIVER ERROR] Failed to free DMA: {} \n", freeResult);
			}
		} else {
			transferMemory_.clear();
			transferMemory_.shrink_to_fit(); // Force memory release immediately
		}

		camera_.reset();

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

void LIBUSB_CALL UsbDriver::transferCallback(struct libusb_transfer* transfer) {
	auto* driver = static_cast<UsbDriver*>(transfer->user_data);
	if (!driver) return;

	const size_t remainingTransfers = driver->activeTransfers_.fetch_sub(1, std::memory_order_acq_rel) - 1;

	if (transfer->status == LIBUSB_TRANSFER_CANCELLED) {
		if (remainingTransfers == 0) {
			driver->running_->store(false, std::memory_order_release);
		}
		return; 
	}

	UsbTransferStatus status = UsbTransferStatus::Error;
	if (transfer->status == LIBUSB_TRANSFER_COMPLETED) {
		status = UsbTransferStatus::Completed;
	} else if (transfer->status == LIBUSB_TRANSFER_NO_DEVICE) {
		status = UsbTransferStatus::Disconnected;
	}

	std::span<const uint8_t> payload;
	if (status == UsbTransferStatus::Completed && transfer->actual_length > 0) {
		payload = std::span<const uint8_t>(transfer->buffer, transfer->actual_length);
	}

	bool shouldResubmit = driver->transferHandler_(status, payload);
	if (!shouldResubmit) {
		driver->running_->store(false, std::memory_order_release);
	} else if (driver->running_->load(std::memory_order_relaxed)) {
		driver->activeTransfers_.fetch_add(1, std::memory_order_relaxed);
		
		if (libusb_submit_transfer(transfer) != LIBUSB_SUCCESS) {
			driver->activeTransfers_.fetch_sub(1, std::memory_order_release);
		}
	}
}
