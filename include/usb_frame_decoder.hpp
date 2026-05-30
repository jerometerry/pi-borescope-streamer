#pragma once

#include "chunk_metadata.hpp"
#include "usb_packet_header.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

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

    std::vector<uint8_t> frameBuffer;
    ChunkMetadata metadata_{};
    
    std::function<void(const std::vector<uint8_t>&)> broadcastHandler;
    std::function<void()> buttonHandler;

    void emitFrame();

    bool isCameraSupported() const;
};