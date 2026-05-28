#pragma once

#include "device_info.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>
#include <utility>

struct libusb_context;
struct libusb_device;
struct libusb_device_handle;

class UsbClient {
public:
    explicit UsbClient();
    ~UsbClient();

    UsbClient(const UsbClient&) = delete;
    UsbClient& operator=(const UsbClient&) = delete;
    UsbClient(UsbClient&&) = delete;
    UsbClient& operator=(UsbClient&&) = delete;

    static int initContext(libusb_context **ctx, const struct libusb_init_option options[], int numOptions);

    static void exit(libusb_context *ctx);

    static int open(libusb_device *device, libusb_device_handle **handle);

    static void close(libusb_device_handle *handle);

    static int read(libusb_device_handle *handle,
                   unsigned char endpoint,
                   std::vector<uint8_t> &buffer,
                   size_t maxSize);

    static int write(libusb_device_handle *handle,
                    unsigned char endpoint,
                    const uint8_t* buffer,
                    size_t length,
                    int& numBytes);

    static int bulkTransfer(libusb_device_handle *dev_handle,
                           unsigned char endpoint, 
                           unsigned char *data, 
                           int length,
                           int *transferred, 
                           unsigned int timeout);

    static int clearHalt(libusb_device_handle *handle, unsigned char endpoint);

    static int setInterfaceAltSetting(libusb_device_handle *dev_handle, int interfaceNumber, int alternateSetting);

    static int claimInterface(libusb_device_handle *handle, int interfaceNumber);

    static int releaseInterface(libusb_device_handle *handle, int interfaceNumber);

    static int kernelDriverActive(libusb_device_handle *handle, int interfaceNumber);

    static int detachKernelDriver(libusb_device_handle *handle, int interfaceNumber);

    static int getStringDescriptorAscii(libusb_device_handle *handle, uint8_t index, unsigned char *data, int length);

    static int getDeviceDescriptor(libusb_device *device, struct libusb_device_descriptor *descriptor);

    static ssize_t getDeviceList(libusb_context *ctx, libusb_device ***list);

    static void freeDeviceList(libusb_device **list, int numDevices);

    static uint8_t getDeviceAddress(libusb_device *device);

    static uint8_t getBusNumber(libusb_device *device);

private:
    libusb_context *context{nullptr};
};