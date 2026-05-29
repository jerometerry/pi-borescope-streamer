#pragma once

#include "usb_context.hpp"
#include <libusb.h>
#include <span>

/** 
 * @brief Class representing a list of USB devices
 */
class UsbDeviceList {
public:
    /** 
     * @brief Construct a new USB device list instance
     * @param context The USB context
     */
    explicit UsbDeviceList(UsbContext& context) :
        count(libusb_get_device_list(context.get(), &devices)) {}
    
    /** 
     * @brief Destroy the USB device list instance
     */
    ~UsbDeviceList() {
        if (devices) {
            // The '1' automatically unrefs the devices in the list
            libusb_free_device_list(devices, 1);
        }
    }

    /** 
     * @brief Copy constructor for the USB device list instance
     */
    UsbDeviceList(const UsbDeviceList&) = delete;

    /** 
     * @brief Assignment operator for the USB device list instance
     * @return A reference to the assigned USB device list instance
     */
    UsbDeviceList& operator=(const UsbDeviceList&) = delete;

    /** 
     * @brief Get the USB device list
     * @return A span containing the USB devices
     */
    std::span<libusb_device*> get() const {
        if (count <= 0 || !devices) return {};
        return {devices, static_cast<size_t>(count)};
    }

private:
    /** 
     * @brief The list of USB devices
     */
    libusb_device** devices{nullptr};

    /** 
     * @brief The number of USB devices in the list
     */
    ssize_t count{0};
};