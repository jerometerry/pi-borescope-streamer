#pragma once
#include <libusb.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>
#include <span>
#include "constants.hpp"
#include "usb_camera.hpp"
#include "usb_device_info.hpp"

/**
 * @brief A zero-cost template wrapper that manages the libusb event loop.
 * @param transferHandler
 * @param running
 */
class UsbDriver {
public:

    using TransferHandler = std::function<bool(UsbTransferStatus, std::span<const uint8_t>)>;

    explicit UsbDriver(TransferHandler transferHandler, std::atomic<bool>* running);

    ~UsbDriver();

    void start(const UsbDeviceInfo& target);

    void stop();

private:
    TransferHandler transferHandler_;
    std::atomic<bool>* running_;
    std::atomic<int> activeTransfers_{0};
    std::unique_ptr<UsbCamera> camera_;
    std::thread workerThread_;
    std::vector<libusb_transfer*> transferPool_;
    std::vector<uint8_t> transferMemory_;

    void loop(const UsbDeviceInfo& target);

    static void LIBUSB_CALL transferCallback(struct libusb_transfer* transfer);
};
