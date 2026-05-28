#pragma once

#include "device_info.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>
#include <utility>

struct libusb_context;
struct libusb_device_handle;

class UsbClient {
public:
    explicit UsbClient();
    ~UsbClient();

    UsbClient(const UsbClient&) = delete;
    UsbClient& operator=(const UsbClient&) = delete;
    UsbClient(UsbClient&&) = delete;
    UsbClient& operator=(UsbClient&&) = delete;

private:
    libusb_context *context{nullptr};
};