#include "usb_client.hpp"
#include <libusb.h>

UsbClient::UsbClient() {
    if (libusb_init_context(&context, nullptr, 0) < 0) {
        throw std::runtime_error("libusb_init_context failed");
    }
}

UsbClient::~UsbClient() {
    if (context) {
        libusb_exit(context);
    }
}
