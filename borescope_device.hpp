#pragma once

#include <cstdint>
#include <vector>
#include <utility>
#include <span>

#include "server_constants.hpp"

struct libusb_context;
struct libusb_device_handle;

class BorescopeDevice {
private:
    static constexpr vid_pid_t VENDOR_PRODUCT_ID_LIST[] = {{0x2ce3, 0x3828}, {0x0329, 0x2022}};
    static constexpr int INTERFACE_A_NUMBER = 0;
    static constexpr int INTERFACE_B_NUMBER = 1;
    static constexpr int INTERFACE_B_ALTERNATE_SETTING = 1;
    static constexpr unsigned char ENDPOINT_1 = 1;
    static constexpr unsigned char ENDPOINT_2 = 2;
    static constexpr unsigned int USB_TIMEOUT = 1000;

    libusb_context *context;
    libusb_device_handle *deviceHandle;

    int usbRead(unsigned char endpoint, byteVector &buffer, size_t max_size);
    int usbWrite(unsigned char endpoint, byteVector buffer);
    libusb_device_handle *openDevice(libusb_context *context, std::span<const vid_pid_t> vendorProductList);

public:
    BorescopeDevice();
    ~BorescopeDevice();

    BorescopeDevice(const BorescopeDevice&) = delete;
    BorescopeDevice& operator=(const BorescopeDevice&) = delete;

    int readFrame(byteVector &readBuffer);
};