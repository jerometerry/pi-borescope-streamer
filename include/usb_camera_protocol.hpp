#pragma once

#include "camera_header.hpp"
#include "usb_frame.hpp"

#include <cstdint>
#include <functional>

/** 
 * @brief Class representing the USB camera protocol
 */
class UsbCameraProtocol {
public:
    /** 
     * @brief Construct a new USB camera protocol instance
     * @param broadcastHandler The handler for broadcasting frames
     * @param buttonHandler The handler for button events
     */
    explicit UsbCameraProtocol(
        std::function<void(const std::vector<uint8_t>&)> broadcastHandler, 
        std::function<void()> buttonHandler
    );

    /** 
     * @brief Handle a frame received from the USB camera
     * @param frame The frame to handle
     */
    void handleFrame(const std::vector<uint8_t> &frame);

private:

    /** 
     * @brief The USB frame header
     */
    static constexpr uint16_t USB_FRAME_HEADER = 0xBBAA;

    /** 
     * @brief The length of the USB header
     */
    static constexpr size_t USB_HEADER_LENGTH = sizeof(UsbFrame);

    /** 
     * @brief The length of the camera header
     */
    static constexpr size_t CAMERA_HEADER_LENGTH = sizeof(CameraHeader);

    /** 
     * @brief The frame buffer
     */
    std::vector<uint8_t> frameBuffer;

    /** 
     * @brief The camera header
     */
    CameraHeader cameraHeader{};
    
    /** 
     * @brief The broadcast handler
     */
    std::function<void(const std::vector<uint8_t>&)> broadcastHandler;

    /** 
     * @brief The button handler
     */
    std::function<void()> buttonHandler;

    /** 
     * @brief Emit the current frame
     */
    void emitFrame();

    /** 
     * @brief Check if the camera is supported
     * @return True if the camera is supported, false otherwise
     */
    bool isCameraSupported() const;
};