#include "usb_client.hpp"
#include "server_constants.hpp"
#include <stdexcept>
#include <span>
#include <libusb.h>

UsbClient::UsbClient() {
    if (initContext(&context, nullptr, 0) < 0) {
        throw std::runtime_error("libusb_init_context failed");
    }
}

UsbClient::~UsbClient() {
    if (context) {
        exit(context);
        context = nullptr;
    }
}

int UsbClient::initContext(libusb_context **ctx, const struct libusb_init_option options[], int numOptions) {
    return libusb_init_context(ctx, options, numOptions);
}

void UsbClient::exit(libusb_context *ctx) {
    libusb_exit(ctx);
}

int UsbClient::open(libusb_device *device, libusb_device_handle **handle) {
    return libusb_open(device, handle);
}

void UsbClient::close(libusb_device_handle *handle) {
    if (handle) {
        libusb_close(handle);
    }
}

int UsbClient::read(libusb_device_handle *handle, 
                    unsigned char endpoint, 
                    std::vector<uint8_t> &buffer, 
                    size_t maxSize) {
    int numBytes = 0;
    size_t readSize = std::min(maxSize, buffer.capacity());
    buffer.resize(readSize);

    int error = bulkTransfer(
        handle, 
        LIBUSB_ENDPOINT_IN | endpoint, 
        buffer.data(), 
        readSize, 
        &numBytes, 
        ServerConstants::USB_TIMEOUT
    );

    if (error != 0) {
        buffer.resize(0);
        return error;
    }

    buffer.resize(numBytes);
    return 0;
}

int UsbClient::write(libusb_device_handle *handle, 
                     unsigned char endpoint, 
                     const uint8_t* buffer,
                     size_t length, 
                     int& numBytes) {
    return bulkTransfer(
        handle, 
        LIBUSB_ENDPOINT_OUT | endpoint, 
        const_cast<unsigned char*>(buffer), 
        length, 
        &numBytes, 
        ServerConstants::USB_TIMEOUT
    );
}

int UsbClient::bulkTransfer(libusb_device_handle *dev_handle,
                            unsigned char endpoint, 
                            unsigned char *data, 
                            int length,
                            int *transferred, 
                            unsigned int timeout) {
    return libusb_bulk_transfer(dev_handle, endpoint, data, length, transferred, timeout);
}

int UsbClient::clearHalt(libusb_device_handle *handle, unsigned char endpoint) {
    return libusb_clear_halt(handle, endpoint);
}

int UsbClient::setInterfaceAltSetting(libusb_device_handle *dev_handle, 
                                      int interfaceNumber, 
                                      int alternateSetting) {
    return libusb_set_interface_alt_setting(dev_handle, interfaceNumber, alternateSetting);
}

int UsbClient::claimInterface(libusb_device_handle *handle, int interfaceNumber) {
    return libusb_claim_interface(handle, interfaceNumber);
}

int UsbClient::releaseInterface(libusb_device_handle *handle, int interfaceNumber) {
    return libusb_release_interface(handle, interfaceNumber);
}

int UsbClient::kernelDriverActive(libusb_device_handle *handle, int interfaceNumber) {
    return libusb_kernel_driver_active(handle, interfaceNumber);
}

int UsbClient::detachKernelDriver(libusb_device_handle *handle, int interfaceNumber) {
    return libusb_detach_kernel_driver(handle, interfaceNumber);
}

int UsbClient::getStringDescriptorAscii(libusb_device_handle *handle,
                                        uint8_t index,
                                        unsigned char *data,
                                        int length) {
    return libusb_get_string_descriptor_ascii(handle, index, data, length);
}

int UsbClient::getDeviceDescriptor(libusb_device *device, struct libusb_device_descriptor *descriptor) {
    return libusb_get_device_descriptor(device, descriptor);
}

ssize_t UsbClient::getDeviceList(libusb_context *ctx, libusb_device ***list) {
    return libusb_get_device_list(ctx, list);
}   

void UsbClient::freeDeviceList(libusb_device **list, int numDevices) {
    libusb_free_device_list(list, numDevices);
}

uint8_t UsbClient::getDeviceAddress(libusb_device *device) {
    return libusb_get_device_address(device);
}

uint8_t UsbClient::getBusNumber(libusb_device *device) {
    return libusb_get_bus_number(device);
}