#include "server_constants.hpp"
#include "usb_camera.hpp"
#include <stdexcept>
#include <span>
#include <libusb.h>

static uint8_t INITIALIZATION_TOKENS[] = {0xFF, 0x55, 0xFF, 0x55, 0xEE, 0x10};
static uint8_t START_STREAM_TOKENS[] = {0xBB, 0xAA, 5, 0, 0};

UsbCamera::UsbCamera() : context(nullptr), deviceHandle(nullptr) {
    if (libusb_init(&context) < 0) {
        throw std::runtime_error("libusb_init failed");
    }

    deviceHandle = open(context);
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

    write(ENDPOINT_2, INITIALIZATION_TOKENS, sizeof(INITIALIZATION_TOKENS));
    write(ENDPOINT_1, START_STREAM_TOKENS, sizeof(START_STREAM_TOKENS));
}

UsbCamera::~UsbCamera() {
    if (deviceHandle) {
        libusb_close(deviceHandle);
    }
    if (context) {
        libusb_exit(context);
    }
}

int UsbCamera::readFrame(std::vector<uint8_t> &frameBuffer) {
    return read(ENDPOINT_1, frameBuffer, ServerConstants::ONE_KILOBYTE);
}

libusb_device_handle* UsbCamera::open(libusb_context *context) {
    struct libusb_device **devices;
    struct libusb_device_handle *handle = nullptr;

    if (libusb_get_device_list(context, &devices) < 0) {
        return nullptr;
    }

    size_t index = 0;
    struct libusb_device *device;

    while ((device = devices[index++]) != nullptr) {
        struct libusb_device_descriptor descriptor;
        if (libusb_get_device_descriptor(device, &descriptor) < 0) {
            continue;
        }
        
        for (const auto &vendorProduct : std::span(VENDOR_PRODUCT_ID_LIST)) {
            if (descriptor.idVendor == vendorProduct.first && descriptor.idProduct == vendorProduct.second) {
                libusb_open(device, &handle);
                break;
            }
        }
        if (handle) {
            break;
        }
    }

    libusb_free_device_list(devices, 1);
    return handle;
}

int UsbCamera::read(unsigned char endpoint, std::vector<uint8_t> &buffer, size_t maxSize) {
    int numBytes = 0;

    // Ensure we don't overflow the pre-allocated capacity
    size_t readSize = std::min(maxSize, buffer.capacity());

    buffer.resize(readSize);

    int error = libusb_bulk_transfer(
        deviceHandle, 
        LIBUSB_ENDPOINT_IN | endpoint, 
        buffer.data(), 
        readSize, 
        &numBytes, 
        USB_TIMEOUT
    );

    if (error != 0) {
        buffer.resize(0);
        return error;
    }

    buffer.resize(numBytes);
    return 0;
}

int UsbCamera::write(unsigned char endpoint, uint8_t* buffer, size_t length) {
    int numBytes = 0;
    return libusb_bulk_transfer(
        deviceHandle, 
        LIBUSB_ENDPOINT_OUT | endpoint, 
        buffer, 
        length, 
        &numBytes, 
        USB_TIMEOUT
    );
}