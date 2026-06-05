#include <libusb.h>
#include <stdexcept>
#include "usb_context.hpp"

UsbContext::UsbContext() {
    if (libusb_init_context(&context_, nullptr, 0) < 0) { 
        throw std::runtime_error("Failed to initialize libusb"); 
    }
}

UsbContext::~UsbContext() {
    if (context_) { 
        libusb_exit(context_);
    }
}

libusb_context* UsbContext::get() { 
    return context_; 
}