#pragma once

struct libusb_context;

class UsbContext {
public:
    UsbContext();
    ~UsbContext();
    UsbContext(const UsbContext&) = delete;
    UsbContext& operator=(const UsbContext&) = delete;

    libusb_context* get();

private:
    libusb_context* context{nullptr};
};