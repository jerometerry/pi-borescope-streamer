#pragma once

#include <libusb.h>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "device_info.hpp"

/** 
 * @brief Class representing a USB camera
 */
class UsbCamera {
public:
    /** 
     * @brief Construct a new USB camera instance
     */
    explicit UsbCamera(const DeviceInfo& target);

    /** 
     * @brief Destroy the USB camera instance
     */
    ~UsbCamera();

    /** 
     * @brief Copy constructor for the USB camera instance
     */
    UsbCamera(const UsbCamera&) = delete;

    /** 
     * @brief Assignment operator for the USB camera instance
     * @return A reference to the assigned USB camera instance
     */
    UsbCamera& operator=(const UsbCamera&) = delete;

    /** 
     * @brief Move constructor for the USB camera instance
     */
    UsbCamera(UsbCamera&&) = delete;

    /** 
     * @brief Move assignment operator for the USB camera instance
     * @return A reference to the moved USB camera instance
     */
    UsbCamera& operator=(UsbCamera&&) = delete;

    /** 
     * @brief Open the USB camera
     * @param context The libusb context
     * @param target The target USB device
     * @return True if the camera was opened successfully, false otherwise
     */
    static libusb_device_handle *open(libusb_context *context, const DeviceInfo& target);

    /** 
     * @brief List all available USB cameras
     * @return A vector of available USB camera devices
     */
    static std::vector<DeviceInfo> listCameras();

    /** 
     * @brief Read data from the USB camera
     * @param buffer The buffer to store the data
     * @return The number of bytes read, or a negative value on error
     */
    int read(std::vector<uint8_t> &buffer);

    /** 
     * @brief Read data from the USB camera
     * @param buffer The buffer to store the data
     * @param maxSize The maximum number of bytes to read
     * @param numBytes The number of bytes read
     * @return The number of bytes read, or a negative value on error
     */
    int read(uint8_t* buffer, size_t maxSize, int& numBytes);

    /** 
     * @brief Read data from the USB camera
     * @param endpoint The endpoint to read from
     * @param buffer The buffer to store the data
     * @param maxSize The maximum number of bytes to read
     * @param numBytes The number of bytes read
     * @return The number of bytes read, or a negative value on error
     */
    int read(unsigned char endpoint, uint8_t* buffer, size_t maxSize, int& numBytes);

    /** 
     * @brief Read data from the USB camera
     * @param endpoint The endpoint to read from
     * @param buffer The buffer to store the data
     * @param maxSize The maximum number of bytes to read
     * @param numBytes The number of bytes read
     * @return The number of bytes read, or a negative value on error
     */
    int read(unsigned char endpoint, std::vector<uint8_t> &buffer, size_t maxSize, int& numBytes);

    /** 
     * @brief Write data to the USB camera
     * @param endpoint The endpoint to write to
     * @param buffer The buffer containing the data to write
     * @param length The length of the data to write
     * @param numBytes The number of bytes written
     * @return The number of bytes written, or a negative value on error
     */
    int write(unsigned char endpoint, const uint8_t* buffer, size_t length, int& numBytes);

private:
    /** 
     * @brief The number of the interface A
     */
    static constexpr int INTERFACE_A_NUMBER = 0;

    /** 
     * @brief The number of the interface B
     */
    static constexpr int INTERFACE_B_NUMBER = 1;

    /** 
     * @brief The alternate setting for interface B
     */
    static constexpr int INTERFACE_B_ALTERNATE_SETTING = 1;

    /** 
     * @brief The first endpoint
     */
    static constexpr unsigned char ENDPOINT_1 = 1;

    /** 
     * @brief The second endpoint
     */
    static constexpr unsigned char ENDPOINT_2 = 2;

    /**
     * @brief The USB timeout value
     */
    static constexpr unsigned int USB_TIMEOUT = 1000;

    /**
     * @brief The libusb context
     */
    libusb_context *context{nullptr};

    /** 
     * @brief The USB device handle
     */
    libusb_device_handle *deviceHandle{nullptr};
};