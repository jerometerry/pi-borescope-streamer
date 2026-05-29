#include "server_constants.hpp"
#include "usb_camera.hpp"
#include <stdexcept>
#include <span>
#include <libusb.h>

static constexpr uint8_t INITIALIZATION_TOKENS[] = {0xFF, 0x55, 0xFF, 0x55, 0xEE, 0x10};
static constexpr uint8_t START_STREAM_TOKENS[] = {0xBB, 0xAA, 5, 0, 0};

UsbCamera::UsbCamera() {
}

UsbCamera::~UsbCamera() {
    close();
}

bool UsbCamera::open(libusb_device_handle* handle) {
    deviceHandle = handle;
    
    if (!deviceHandle) {
        return false;
    }

    for (int iface : {INTERFACE_A_NUMBER, INTERFACE_B_NUMBER}) {
        if (libusb_kernel_driver_active(deviceHandle, iface) == 1) {
            libusb_detach_kernel_driver(deviceHandle, iface);
        }
    }

    if (libusb_claim_interface(deviceHandle, INTERFACE_A_NUMBER) < 0 ||
        libusb_claim_interface(deviceHandle, INTERFACE_B_NUMBER) < 0) {
        throw std::runtime_error("Failed to claim USB hardware interfaces");
    }

    // --- Heartbeat Draining (Interface 0) ---
    // Loop 30 times with a rapid 100ms timeout to clear out pending heartbeat data 
    // from the iAP interface before we attempt to stream.
    int drainBytes = 0;
    unsigned char drainBuf[512];
    for (int i = 0; i < 30; ++i) {
        int err = libusb_bulk_transfer(
            deviceHandle, 
            LIBUSB_ENDPOINT_IN | 0x02, 
            drainBuf, 
            sizeof(drainBuf), 
            &drainBytes, 
            100
        );

        if (err == LIBUSB_ERROR_TIMEOUT) {
            break;
        }
    }

    if (libusb_set_interface_alt_setting(deviceHandle, INTERFACE_B_NUMBER, INTERFACE_B_ALTERNATE_SETTING) < 0) {
        throw std::runtime_error("libusb_set_interface_alt_setting failed");
    }

    libusb_clear_halt(deviceHandle, ENDPOINT_1);

    write(ENDPOINT_2, INITIALIZATION_TOKENS, sizeof(INITIALIZATION_TOKENS));
    write(ENDPOINT_1, START_STREAM_TOKENS, sizeof(START_STREAM_TOKENS));

    return true;
}

bool UsbCamera::close() {
    if (deviceHandle) {
        libusb_close(deviceHandle);
        deviceHandle = nullptr;
    }
    return true;
}

int UsbCamera::read(std::vector<uint8_t> &buffer, size_t length, int& bytesRead) {
    return read(ENDPOINT_1, buffer, length, bytesRead);
}

int UsbCamera::read(unsigned char endpoint, std::vector<uint8_t> &buffer, size_t length, int& bytesRead) {
    size_t readSize = std::min(length, buffer.size());

    return libusb_bulk_transfer(
        deviceHandle, 
        LIBUSB_ENDPOINT_IN | endpoint, 
        buffer.data(), 
        readSize, 
        &bytesRead, 
        ServerConstants::USB_TIMEOUT
    );
}

int UsbCamera::write(unsigned char endpoint, const uint8_t* buffer, size_t length) {
    int numBytes = 0;
    return libusb_bulk_transfer(
        deviceHandle, 
        LIBUSB_ENDPOINT_OUT | endpoint, 
        const_cast<unsigned char*>(buffer), 
        length, 
        &numBytes, 
        ServerConstants::USB_TIMEOUT
    );
}