#include "borescope_device.hpp"
#include "server_constants.hpp"
#include <libusb-1.0/libusb.h>
#include <stdexcept>

BorescopeDevice::BorescopeDevice() : context(nullptr), deviceHandle(nullptr) {
    if (libusb_init(&context) < 0) {
        throw std::runtime_error("libusb_init failed");
    }

    deviceHandle = openDevice(context, std::span(VENDOR_PRODUCT_ID_LIST));
    if (!deviceHandle) {
        throw std::runtime_error("Borescope hardware device not found on USB bus");
    }

    if (libusb_claim_interface(deviceHandle, INTERFACE_A_NUMBER) < 0 ||
        libusb_claim_interface(deviceHandle, INTERFACE_B_NUMBER) < 0) {
        throw std::runtime_error("Failed to claim USB hardware interfaces");
    }

    if (libusb_set_interface_alt_setting(deviceHandle, INTERFACE_B_NUMBER, INTERFACE_B_ALTERNATE_SETTING) < 0) {
        throw std::runtime_error("libusb_set_interface_alt_setting failed");
    }

    libusb_clear_halt(deviceHandle, ENDPOINT_1);

    byteVector initializationToken = {0xFF, 0x55, 0xFF, 0x55, 0xEE, 0x10};
    usbWrite(ENDPOINT_2, initializationToken);
    
    byteVector startStreamToken = {0xBB, 0xAA, 5, 0, 0};
    usbWrite(ENDPOINT_1, startStreamToken);
}

BorescopeDevice::~BorescopeDevice() {
    if (deviceHandle) {
        libusb_close(deviceHandle);
    }
    if (context) {
        libusb_exit(context);
    }
}

int BorescopeDevice::usbRead(unsigned char endpoint, byteVector &buffer, size_t maxSize) {
    int transferredBytes = 0;
    buffer.resize(maxSize);

    int returnStatus = libusb_bulk_transfer(
        deviceHandle, 
        LIBUSB_ENDPOINT_IN | endpoint, 
        buffer.data(), 
        buffer.size(), 
        &transferredBytes, 
        USB_TIMEOUT
    );
    if (returnStatus != 0) {
        buffer.resize(0);
        return returnStatus;
    }

    buffer.resize(transferredBytes);
    return 0;
}

int BorescopeDevice::usbWrite(unsigned char endpoint, byteVector buffer) {
    int transferredBytes = 0;
    return libusb_bulk_transfer(
        deviceHandle, 
        LIBUSB_ENDPOINT_OUT | endpoint, 
        buffer.data(), 
        buffer.size(), 
        &transferredBytes, 
        USB_TIMEOUT
    );
}

libusb_device_handle* BorescopeDevice::openDevice(libusb_context *usbContext, std::span<const vid_pid_t> vendorProductList) {
    struct libusb_device **deviceList;
    struct libusb_device_handle *discoveredHandle = nullptr;

    if (libusb_get_device_list(usbContext, &deviceList) < 0) {
        return nullptr;
    }

    size_t index = 0;
    struct libusb_device *device;
    while ((device = deviceList[index++]) != nullptr) {
        struct libusb_device_descriptor descriptor;
        if (libusb_get_device_descriptor(device, &descriptor) < 0) {
            continue;
        }
        
        for (const auto &vendorProduct : vendorProductList) {
            if (descriptor.idVendor == vendorProduct.first && descriptor.idProduct == vendorProduct.second) {
                libusb_open(device, &discoveredHandle);
                break;
            }
        }
        if (discoveredHandle) {
            break;
        }
    }

    libusb_free_device_list(deviceList, 1);
    return discoveredHandle;
}

int BorescopeDevice::readFrame(byteVector &readBuffer) {
    return usbRead(ENDPOINT_1, readBuffer, ServerConstants::ONE_KILOBYTE);
}