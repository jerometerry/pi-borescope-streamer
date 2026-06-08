#pragma once

#include <cstdint>
#include <string>

/**
 * @brief The physical identification badge for a piece of hardware plugged into the system.
 * @details Before the software can start pumping video, it needs to know exactly where the 
 * camera is and who made it. This structure holds the physical "address" of the device on 
 * the motherboard, along with the manufacturer's embedded ID tags (Vendor and Product IDs).
 */
struct UsbDeviceInfo {
    /**
     * @brief The physical hardware lane the device is plugged into (e.g., the top USB row vs bottom row).
     */
    uint8_t bus{0};

    /**
     * @brief The exact slot number on that specific bus.
     */
    uint8_t address{0};

    /**
     * @brief The manufacturer's global company code (e.g., 0x0C45 for Microdia).
     */
    uint16_t vendorId{0};

    /**
     * @brief The manufacturer's specific model code for this exact device.
     */
    uint16_t productId{0};

    /**
     * @brief The human-readable name of the company that built the hardware, if available.
     */
    std::string manufacturer{""};

    /**
     * @brief The human-readable name of the device, if available.
     */
    std::string product{""};

    /**
     * @brief The unique serial number stamped into this specific camera, if available.
     */
    std::string serialNumber{""};

    /**
     * @brief A quick-check flag verifying if this device's ID matches our supported endoscopes.
     */
    bool isSuperCamera{false};

    /**
     * @brief Verify if two ID badges belong to the exact same brand and model of camera.
     * @param device The other ID badge to compare against.
     * @return True if both devices share the exact same Vendor and Product IDs.
     */
    bool isSameDevice(const DeviceInfo& device) const {
        return vendorId == device.vendorId && productId == device.productId;
    }
};
