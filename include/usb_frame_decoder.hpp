#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <span>
#include <vector>
#include "data_structures.hpp"

/** 
 * @brief Class representing the USB frame decoder
 */
class UsbFrameDecoder {
public:
    /** 
     * @brief Construct a new USB frame decoder instance
     * @param broadcastHandler The handler for broadcasting decoded frames
     * @param buttonHandler The handler for button events
     */
    explicit UsbFrameDecoder(
        std::function<void(const std::vector<uint8_t>&)> broadcastHandler, 
        std::function<void()> buttonHandler
    );

    /** 
     * @brief Process incoming camera data
     * @param data The incoming camera data
     */
    void processIncomingCameraData(std::span<const uint8_t> data);

private:

    /** 
     * @brief The length of a USB packet header
     */
    static constexpr size_t USB_PACKET_HEADER_LENGTH = sizeof(UsbPacketHeader);

    /** 
     * @brief The length of chunk metadata
     */
    static constexpr size_t CHUNK_METADATA_LENGTH = sizeof(ChunkMetadata);

    /** 
     * @brief The buffer for streaming data
     */
    std::vector<uint8_t> streamBuffer;

    /** 
     * @brief The buffer for frame data
     */
    std::vector<uint8_t> frameBuffer;

    /** 
     * @brief The buffer for emitted data
     */
    std::vector<uint8_t> emitBuffer;

    /** 
     * @brief The metadata for the current chunk
     */
    ChunkMetadata metadata_{};

    /** 
     * @brief The offset for reading data
     */
    size_t readOffset{0};
    
    /** 
     * @brief The handler for broadcasting decoded frames
     */
    std::function<void(const std::vector<uint8_t>&)> broadcastHandler;

    /** 
     * @brief The handler for button events
     */
    std::function<void()> buttonHandler;

    /** 
     * @brief Trim and emit the current frame
     */
    void trimAndEmitFrame();

    /** 
     * @brief Check if the chunk is from the video feed
     * @param metadata The chunk metadata
     * @return true if the chunk is from the video feed, false otherwise
     */
    static bool fromVideoFeed(ChunkMetadata metadata);

     /** 
     * @brief Check if the first header is for the same camera as the second header
     * @param first The other camera header to compare against
     * @param second The other camera header to compare against
     * @return true if the headers are for the same camera, false otherwise
     */
    static bool forSameCamera(ChunkMetadata first, ChunkMetadata second);

    /** 
     * @brief Check if the first header is for the same camera as the second header
     * @param first The other camera header to compare against
     * @param second The other camera header to compare against
     * @return true if the headers are for the same camera, false otherwise
     */
    static bool forSameCameraAndFrame(ChunkMetadata first, ChunkMetadata second);
};
