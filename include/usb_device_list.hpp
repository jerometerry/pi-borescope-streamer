#pragma once

#include "usb_context.hpp"
#include <libusb.h>
#include <span>

class UsbDeviceList {
public:
    explicit UsbDeviceList(UsbContext& context) :
        count(libusb_get_device_list(context.get(), &devices)) {}
    
    ~UsbDeviceList() {
        if (devices) {
            // The '1' automatically unrefs the devices in the list
            libusb_free_device_list(devices, 1);
        }
    }

    // Rule of Zero / Prevent unsafe copying
    UsbDeviceList(const UsbDeviceList&) = delete;
    UsbDeviceList& operator=(const UsbDeviceList&) = delete;

    // Bridge the raw C-array to a modern C++ span
    std::span<libusb_device*> get() const {
        if (count <= 0 || !devices) return {};
        return {devices, static_cast<size_t>(count)};
    }

private:
    libusb_device** devices{nullptr};
    ssize_t count{0};
};