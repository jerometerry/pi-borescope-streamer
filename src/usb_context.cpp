#include <libusb.h>
#include <stdexcept>
#include "usb_context.hpp"

UsbContext::UsbContext() {
    if (libusb_init_context(&context, nullptr, 0) < 0) { 
        throw std::runtime_error("Failed to initialize libusb"); 
    }
}

UsbContext::~UsbContext() {
    if (context) { 
        libusb_exit(context);
    }
}

libusb_context* UsbContext::get() { 
    return context; 
}