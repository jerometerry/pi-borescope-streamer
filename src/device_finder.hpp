#pragma once

#include "device_info.hpp"
#include "usb_context.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>
#include <utility>

struct libusb_context;
struct libusb_device_handle;

namespace DeviceFinder {
    std::vector<DeviceInfo> list(bool onlySuperCameras = true);

    libusb_device_handle* open(UsbContext& context, const DeviceInfo& target);

    inline constexpr std::pair<uint16_t, uint16_t> VENDOR_PRODUCT_ID_LIST[] = 
        {{0x2ce3, 0x3828}, {0x0329, 0x2022}};
};