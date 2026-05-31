#pragma once

#include <libusb.h>

/** 
 * @brief Class representing a USB context
 */
class UsbContext {
public:
    /** 
     * @brief Construct a new USB context instance
     */
    UsbContext();

    /** 
     * @brief Destroy the USB context instance
     */
    ~UsbContext();

    /** 
     * @brief Copy constructor for the USB context instance
     */
    UsbContext(const UsbContext&) = delete;

    /** 
     * @brief Assignment operator for the USB context instance
     * @return A reference to the assigned USB context instance
     */
    UsbContext& operator=(const UsbContext&) = delete;

    /** 
     * @brief Get the USB context
     * @return A pointer to the USB context
     */
    libusb_context* get();

private:
    /** 
     * @brief The USB context
     */
    libusb_context* context{nullptr};
};