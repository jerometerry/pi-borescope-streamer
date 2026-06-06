#include <libusb.h>
#include <sys/types.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include "constants.hpp"
#include "usb_camera.hpp"

UsbCamera::UsbCamera(const DeviceInfo& target) {
    if (libusb_init(&context_) < 0) {
        throw std::runtime_error("libusb_init failed");
    }

    deviceHandle_ = open(context_, target);
    if (!deviceHandle_) {
        throw std::runtime_error("Specified Borescope hardware device not found on USB bus");
    }

    for (int iface : {UsbProtocol::INTERFACE_A_NUMBER, UsbProtocol::INTERFACE_B_NUMBER}) {
        if (libusb_kernel_driver_active(deviceHandle_, iface) == 1) {
            libusb_detach_kernel_driver(deviceHandle_, iface);
        }
    }

    if (libusb_claim_interface(deviceHandle_, UsbProtocol::INTERFACE_A_NUMBER) < 0 ||
        libusb_claim_interface(deviceHandle_, UsbProtocol::INTERFACE_B_NUMBER) < 0) {
        throw std::runtime_error("Failed to claim USB hardware interfaces");
    }

    // Loop 30 times with a rapid 100ms timeout to clear out pending heartbeat data 
    // from the iAP interface before we attempt to stream.
    int drainBytes = 0;
    unsigned char drainBuf[512];
    for (int i = 0; i < 30; ++i) {
        // 0x02 is the iAP IN endpoint (LIBUSB_ENDPOINT_IN adds the 0x80 bit to make it 0x82)
        libusb_bulk_transfer(deviceHandle_, LIBUSB_ENDPOINT_IN | 0x02, drainBuf, sizeof(drainBuf), &drainBytes, 100);
    }

    if (libusb_set_interface_alt_setting(deviceHandle_, UsbProtocol::INTERFACE_B_NUMBER, UsbProtocol::INTERFACE_B_ALTERNATE_SETTING) < 0) {
        throw std::runtime_error("libusb_set_interface_alt_setting failed");
    }

    libusb_clear_halt(deviceHandle_, UsbProtocol::ENDPOINT_1);

    int numBytes = 0;
    write(
        UsbProtocol::ENDPOINT_2, 
        UsbProtocol::INITIALIZATION_TOKENS, 
        sizeof(UsbProtocol::INITIALIZATION_TOKENS), 
        numBytes
    );

    write(
        UsbProtocol::ENDPOINT_1, 
        UsbProtocol::START_STREAM_TOKENS, 
        sizeof(UsbProtocol::START_STREAM_TOKENS), 
        numBytes
    );
}

UsbCamera::~UsbCamera() {
    if (deviceHandle_) {
        libusb_close(deviceHandle_);
    }
    if (context_) {
        libusb_exit(context_);
    }
}

libusb_device_handle* UsbCamera::open(libusb_context *context, const DeviceInfo& target) {
    libusb_device** devices = nullptr;
    ssize_t count = libusb_get_device_list(context, &devices);
    if (count < 0) return nullptr;

    libusb_device_handle* handle = nullptr;

    for (ssize_t i = 0; i < count; ++i) {
        libusb_device* device = devices[i];
        if (libusb_get_bus_number(device) == target.bus && 
            libusb_get_device_address(device) == target.address) {
            
            if (libusb_open(device, &handle) == 0) {
                break;
            }
        }
    }

    libusb_free_device_list(devices, 1);
    return handle;
}

std::vector<DeviceInfo> UsbCamera::listCameras() {
    std::vector<DeviceInfo> cameras;
    libusb_context* ctx = nullptr;
    
    if (libusb_init(&ctx) < 0) {
        return cameras;
    }

    libusb_device** devices = nullptr;
    ssize_t count = libusb_get_device_list(ctx, &devices);
    if (count < 0) {
        libusb_exit(ctx);
        return cameras;
    }

    for (ssize_t i = 0; i < count; ++i) {
        libusb_device* device = devices[i];
        struct libusb_device_descriptor desc{};
        
        if (libusb_get_device_descriptor(device, &desc) < 0) continue;

        bool isSupported = std::ranges::any_of(UsbProtocol::VENDOR_PRODUCT_ID_LIST,
            [&desc](const auto& vp) {
                return desc.idVendor == vp.first && desc.idProduct == vp.second;
            });

        if (isSupported) {
            DeviceInfo info{
                .bus = libusb_get_bus_number(device),
                .address = libusb_get_device_address(device),
                .vendorId = desc.idVendor,
                .productId = desc.idProduct,
                .manufacturer = "Unknown",
                .product = "Unknown",
                .serialNumber = "Unknown",
                .isSuperCamera = true
            };

            libusb_device_handle* handle = nullptr;
            if (libusb_open(device, &handle) == 0) {
                unsigned char strBuf[256];
                
                if (desc.iManufacturer && libusb_get_string_descriptor_ascii(handle, desc.iManufacturer, strBuf, sizeof(strBuf)) > 0)
                    info.manufacturer = reinterpret_cast<char*>(strBuf);
                    
                if (desc.iProduct && libusb_get_string_descriptor_ascii(handle, desc.iProduct, strBuf, sizeof(strBuf)) > 0)
                    info.product = reinterpret_cast<char*>(strBuf);
                    
                if (desc.iSerialNumber && libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, strBuf, sizeof(strBuf)) > 0)
                    info.serialNumber = reinterpret_cast<char*>(strBuf);
                    
                libusb_close(handle);
            }
            cameras.push_back(info);
        }
    }

    libusb_free_device_list(devices, 1);
    libusb_exit(ctx);
    return cameras;
}

[[nodiscard]] libusb_device_handle* UsbCamera::getRawHandle() const { 
    return deviceHandle_; 
}

[[nodiscard]] libusb_context* UsbCamera::getContext() const { 
    return context_; 
}

int UsbCamera::read(std::vector<uint8_t> &buffer) {
    int numBytes = 0;
    return read(UsbProtocol::ENDPOINT_1, buffer, Units::FOUR_KILOBYTES, numBytes);
}

int UsbCamera::read(uint8_t* buffer, size_t maxSize, int& numBytes) {
    return read(UsbProtocol::ENDPOINT_1, buffer, maxSize, numBytes);
}

int UsbCamera::read(unsigned char endpoint, uint8_t* buffer, size_t maxSize, int& numBytes) {
    return libusb_bulk_transfer(
        deviceHandle_, 
        LIBUSB_ENDPOINT_IN | endpoint, 
        buffer, 
        maxSize, 
        &numBytes, 
        UsbConfig::USB_TIMEOUT
    );
}

int UsbCamera::read(unsigned char endpoint, std::vector<uint8_t> &buffer, size_t maxSize, int& numBytes) {
    buffer.resize(maxSize);

    int error = libusb_bulk_transfer(
        deviceHandle_, 
        LIBUSB_ENDPOINT_IN | endpoint, 
        buffer.data(), 
        maxSize, 
        &numBytes, 
        UsbConfig::USB_TIMEOUT
    );

    if (error != 0) {
        return error;
    }

    buffer.resize(numBytes);
    return 0;
}

int UsbCamera::write(unsigned char endpoint, const uint8_t* buffer, size_t length, int& numBytes) {
    return libusb_bulk_transfer(
        deviceHandle_, 
        LIBUSB_ENDPOINT_OUT | endpoint, 
        const_cast<unsigned char*>(buffer), 
        length, 
        &numBytes, 
        UsbConfig::USB_TIMEOUT
    );
}
