#include <algorithm>
#include <bit>
#include <cstdint>
#include <string>
#include <utility>
#include "chunk_metadata.hpp"
#include "server_constants.hpp"
#include "usb_frame_decoder.hpp"
#include "usb_packet_header.hpp"

static_assert(std::endian::native == std::endian::little);

UsbFrameDecoder::UsbFrameDecoder(
    std::function<void(const std::vector<uint8_t>&)> broadcastHandler, std::function<void()> buttonHandler) 
    : broadcastHandler(std::move(broadcastHandler)), buttonHandler(std::move(buttonHandler)) {
        frameBuffer.reserve(ServerConstants::ONE_MEGABYTE);
        streamBuffer.reserve(ServerConstants::ONE_MEGABYTE);
        emitBuffer.reserve(ServerConstants::ONE_MEGABYTE);
}

void UsbFrameDecoder::trimAndEmitFrame() {
    if (frameBuffer.empty()) {
        return;
    }

    size_t soiOffset = std::string::npos;
    size_t eoiOffset = std::string::npos;

    // Scan forward for Start of Image (FF D8)
    for (size_t j = 0; j + 1 < std::min<size_t>(256, frameBuffer.size()); ++j) {
        if (frameBuffer[j] == 0xFF && frameBuffer[j+1] == 0xD8) {
            soiOffset = j;
            break;
        }
    }

    // Scan backwards for End of Image (FF D9)
    for (size_t j = frameBuffer.size(); j >= 2; --j) {
        if (frameBuffer[j - 2] == 0xFF && frameBuffer[j - 1] == 0xD9) {
            eoiOffset = j;
            break;
        }
    }

    // If we have valid boundaries, slice the pure JPEG and broadcast
    if (soiOffset != std::string::npos && eoiOffset != std::string::npos && soiOffset < eoiOffset) {
        size_t frameSize = eoiOffset - soiOffset;
        emitBuffer.clear();

        if (emitBuffer.capacity() < frameSize) {
            emitBuffer.reserve(frameSize); 
        }

        emitBuffer.insert(emitBuffer.end(), frameBuffer.begin() + soiOffset, frameBuffer.begin() + eoiOffset);

        if (broadcastHandler) {
            broadcastHandler(emitBuffer);
        }
    }
    
    frameBuffer.clear();
}

void UsbFrameDecoder::processIncomingCameraData(std::span<const uint8_t> data) {
    // 1. Append the new hardware burst to our sliding window
    streamBuffer.insert(streamBuffer.end(), data.begin(), data.end());

    size_t i = 0;
    const size_t TOTAL_HEADER_SIZE = sizeof(UsbPacketHeader) + sizeof(ChunkMetadata);

    // 2. Loop through the stream buffer to extract all available packets
    while (i + TOTAL_HEADER_SIZE <= streamBuffer.size()) {
        
        // Safety lock: Ensure we have enough lookahead buffer to safely run the 300-byte 
        // Ghost Filter. If not, break and wait for the next USB read to fill the window.
        if (i + 300 > streamBuffer.size()) {
            break; 
        }

        const UsbPacketHeader* header = reinterpret_cast<const UsbPacketHeader*>(&streamBuffer[i]);

        if (header->header != ServerConstants::USB_FRAME_HEADER || 
           (header->cameraId != 0x0B && header->cameraId != 0x07)) {
            i++;
            continue;
        }

        // --- THE PROXIMITY GHOST FILTER ---
        bool isGhost = false;
        size_t nextHeaderOffset = 0;
        
        for (size_t d = 5; d <= 300; ++d) {
            if (streamBuffer[i+d] == 0xAA && streamBuffer[i+d+1] == 0xBB && 
               (streamBuffer[i+d+2] == 0x0B || streamBuffer[i+d+2] == 0x07)) {
                isGhost = true;
                nextHeaderOffset = d;
                break;
            }
        }

        if (isGhost) {
            i += nextHeaderOffset;
            continue;
        }

        // Calculate total packet size
        size_t chunkTotalSize = sizeof(UsbPacketHeader) + header->length;

        // If the packet was cut in half by the 64KB USB boundary, wait for the rest!
        if (i + chunkTotalSize > streamBuffer.size()) {
            break;
        }

        const ChunkMetadata* meta = reinterpret_cast<const ChunkMetadata*>(&streamBuffer[i + sizeof(UsbPacketHeader)]);

        // Frame Assembly Logic
        if (!frameBuffer.empty() && metadata_.frameId != meta->frameId) {
            trimAndEmitFrame();
        }
        metadata_ = *meta;

        // Button Event Routing
        if (meta->isButtonPressed() && buttonHandler) {
            buttonHandler();
        }

        // Video Routing (Ignore Telemetry Packets)
        if (!meta->hasGravitySensor() && meta->getOtherFlags() == 0 && meta->cameraNumber < 2) {
            size_t payloadStart = i + TOTAL_HEADER_SIZE;
            size_t payloadSize = chunkTotalSize - TOTAL_HEADER_SIZE;
            frameBuffer.insert(frameBuffer.end(), 
                               streamBuffer.begin() + payloadStart, 
                               streamBuffer.begin() + payloadStart + payloadSize);
        }

        // Leapfrog over the fully processed packet
        i += chunkTotalSize;
    }

    // 3. Clear processed bytes from the sliding window, keeping any incomplete packets
    streamBuffer.erase(streamBuffer.begin(), streamBuffer.begin() + i);
}