#pragma once

#include "device_info.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>
#include <utility>

struct libusb_context;
struct libusb_device_handle;

class UsbCamera {
public:
    explicit UsbCamera(const DeviceInfo& target);
    ~UsbCamera();

    UsbCamera(const UsbCamera&) = delete;
    UsbCamera& operator=(const UsbCamera&) = delete;
    UsbCamera(UsbCamera&&) = delete;
    UsbCamera& operator=(UsbCamera&&) = delete;

    int readFrame(std::vector<uint8_t> &frameBuffer);

private:
    static constexpr std::pair<uint16_t, uint16_t> VENDOR_PRODUCT_ID_LIST[] = {{0x2ce3, 0x3828}, {0x0329, 0x2022}};
    static constexpr int INTERFACE_A_NUMBER = 0;
    static constexpr int INTERFACE_B_NUMBER = 1;
    static constexpr int INTERFACE_B_ALTERNATE_SETTING = 1;
    static constexpr unsigned char ENDPOINT_1 = 1;
    static constexpr unsigned char ENDPOINT_2 = 2;
    static constexpr unsigned int USB_TIMEOUT = 1000;

    libusb_context *context{nullptr};
    libusb_device_handle *deviceHandle{nullptr};

    static libusb_device_handle *open(libusb_context *context, const DeviceInfo& target);

    int read(unsigned char endpoint, std::vector<uint8_t> &buffer, size_t maxSize);
    int write(unsigned char endpoint, const uint8_t* buffer, size_t length);
};