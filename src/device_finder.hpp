#pragma once

#include "device_info.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>
#include <utility>

struct libusb_context;
struct libusb_device_handle;

class DeviceFinder {
public:
    explicit DeviceFinder();
    ~DeviceFinder();

    DeviceFinder(const DeviceFinder&) = delete;
    DeviceFinder& operator=(const DeviceFinder&) = delete;
    DeviceFinder(DeviceFinder&&) = delete;
    DeviceFinder& operator=(DeviceFinder&&) = delete;

    static std::vector<DeviceInfo> listDevices(bool onlySuperCameras = true);

private:
    static constexpr std::pair<uint16_t, uint16_t> VENDOR_PRODUCT_ID_LIST[] = {{0x2ce3, 0x3828}, {0x0329, 0x2022}};
};