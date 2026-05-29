#pragma once

#include <cstdint>
#include <string>

/** 
 * @brief Structure representing information about a USB device
 */
struct DeviceInfo {
    /** 
     * @brief The USB bus number
     */
    uint8_t bus;

    /** 
     * @brief The USB device address
     */
    uint8_t address;

    /** 
     * @brief The vendor ID of the device
     */
    uint16_t vendorId;

    /** 
     * @brief The product ID of the device
     */
    uint16_t productId;

    /** 
     * @brief The manufacturer name of the device
     */
    std::string manufacturer;

    /** 
     * @brief The product name of the device
     */
    std::string product;

    /** 
     * @brief The serial number of the device
     */
    std::string serialNumber;

    /** 
     * @brief Flag indicating if the device is a super camera
     */
    bool isSuperCamera;

    /** 
     * @brief Check if this device is the same as another device
     * @param device The device to compare against
     * @return True if the devices are the same, false otherwise
     */
    bool isSameDevice(const DeviceInfo& device) const {
        return vendorId == device.vendorId && productId == device.productId;
    }
};