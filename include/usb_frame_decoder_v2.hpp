#pragma once

#include "usb_packet_header.hpp"
#include "chunk_metadata.hpp"
#include <cstdint>
#include <functional>
#include <span>

/** 
 * @brief Class representing the USB camera protocol
 */
class UsbFrameDecoderV2 {
public:
    /** 
     * @brief Construct a new USB camera protocol instance
     * @param broadcastHandler The handler for broadcasting frames
     * @param buttonHandler The handler for button events
     */
    explicit UsbFrameDecoderV2(
        std::function<void(const std::vector<uint8_t>&)> broadcastHandler, 
        std::function<void()> buttonHandler
    );

    /** 
     * @brief Handle a frame received from the USB camera
     * @param readBuffer Data streamed from the camera
     */
    void processIncomingCameraData(std::span<const uint8_t> data);

private:

    /** 
     * @brief The USB frame header
     */
    static constexpr uint16_t USB_FRAME_HEADER = 0xBBAA;

    /** 
     * @brief The length of the USB frame
     */
    static constexpr size_t USB_FRAME_LENGTH = sizeof(UsbPacketHeader);

    /** 
     * @brief The length of the camera frame
     */
    static constexpr size_t CAMERA_FRAME_LENGTH = sizeof(ChunkMetadata);

    /** 
     * @brief The frame buffer
     */
    std::vector<uint8_t> frameBuffer;

    /** 
     * @brief The camera frame
     */
    ChunkMetadata cameraFrame{};
    
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