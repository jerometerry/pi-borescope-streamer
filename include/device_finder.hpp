#pragma once

#include "device_info.hpp"
#include <cstdint>
#include <utility>
#include <vector>

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

    /** 
     * @brief A list of vendor and product IDs for supported devices
     */
    inline constexpr std::pair<uint16_t, uint16_t> VENDOR_PRODUCT_ID_LIST[] = 
        {{0x2ce3, 0x3828}, {0x0329, 0x2022}};
};