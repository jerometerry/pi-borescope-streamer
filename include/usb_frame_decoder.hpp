#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <span>
#include <vector>
#include "data_structures.hpp"

class UsbFrameDecoder {
public:
    explicit UsbFrameDecoder(
        std::function<void(const std::vector<uint8_t>&)> broadcastHandler, 
        std::function<void()> buttonHandler
    );

    void processIncomingCameraData(std::span<const uint8_t> data);

private:
    static constexpr size_t USB_PACKET_HEADER_LENGTH = sizeof(UsbPacketHeader);
    static constexpr size_t CHUNK_METADATA_LENGTH = sizeof(ChunkMetadata);

    std::vector<uint8_t> streamBuffer;
    std::vector<uint8_t> frameBuffer;
    std::vector<uint8_t> emitBuffer;
    ChunkMetadata metadata_{};

    size_t readOffset{0};
    
    std::function<void(const std::vector<uint8_t>&)> broadcastHandler;
    std::function<void()> buttonHandler;

    void trimAndEmitFrame();

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
