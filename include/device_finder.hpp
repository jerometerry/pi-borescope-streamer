#pragma once

#include <vector>
#include "device_info.hpp"

class UsbContext;
struct libusb_device_handle;

/** 
 * @brief Namespace for finding and managing USB devices
 */
namespace DeviceFinder {
    /** 
     * @brief Find all available USB devices
     * @return A vector of DeviceInfo structures representing the found devices
     */
    std::vector<DeviceInfo> all();

    /** 
     * @brief Find all super cameras
     * @return A vector of DeviceInfo structures representing the found super cameras
     */
    std::vector<DeviceInfo> superCameras();

    /** 
     * @brief Find USB devices based on a filter
     * @param onlySuperCameras Flag indicating if only super cameras should be returned
     * @return A vector of DeviceInfo structures representing the found devices
     */
    std::vector<DeviceInfo> find(bool onlySuperCameras);

    /** 
     * @brief Open a USB device handle
     * @param context The USB context
     * @param target The device info for the target device
     * @return A pointer to the opened device handle, or nullptr if failed
     */
    libusb_device_handle* open(UsbContext& context, const DeviceInfo& target);
};