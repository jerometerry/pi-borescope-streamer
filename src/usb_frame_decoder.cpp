#include <algorithm>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include "usb_frame_decoder.hpp"
#include "server_constants.hpp"

UsbFrameDecoder::UsbFrameDecoder(
    std::function<void(const std::vector<uint8_t>&)> broadcastHandler, std::function<void()> buttonHandler) 
    : broadcastHandler(std::move(broadcastHandler)), buttonHandler(std::move(buttonHandler)) {
        frameBuffer.reserve(ServerConstants::FORTY_KILOBYTES);
        streamBuffer.reserve(ServerConstants::EIGHT_KILOBYTES);
        emitBuffer.reserve(ServerConstants::FORTY_KILOBYTES);
}

void UsbFrameDecoder::processIncomingCameraData(std::span<const uint8_t> data) {
    streamBuffer.insert(streamBuffer.end(), data.begin(), data.end());

    size_t i = 0;
    const size_t TOTAL_HEADER_SIZE = sizeof(UsbPacketHeader) + sizeof(ChunkMetadata);

    while (i + TOTAL_HEADER_SIZE <= streamBuffer.size()) {
        UsbPacketHeader header{};
        std::memcpy(&header, &streamBuffer[i], sizeof(UsbPacketHeader));

        if (header.header != ServerConstants::USB_FRAME_HEADER || 
           (header.cameraId != 0x0B && header.cameraId != 0x07)) {
            i++;
            continue;
        }

        // Verify if a ghost header lives down the pipe *before* checking data boundaries
        bool isGhost = false;
        size_t nextHeaderOffset = 0;

        size_t maxScan = std::min<size_t>(300, streamBuffer.size() - i - 3); 
        for (size_t d = 5; d <= maxScan; ++d) {
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

        size_t chunkTotalSize = sizeof(UsbPacketHeader) + header.length;

        if (i + chunkTotalSize > streamBuffer.size()) {
            break;
        }

        ChunkMetadata meta{};
        std::memcpy(&meta, &streamBuffer[i + sizeof(UsbPacketHeader)], sizeof(ChunkMetadata));

        if (!frameBuffer.empty() && metadata_.frameId != meta.frameId) {
            trimAndEmitFrame();
        }
        metadata_ = meta;

        if (meta.isButtonPressed() && buttonHandler) {
            buttonHandler();
        }

        if (!meta.hasGravitySensor() && meta.getOtherFlags() == 0 && meta.cameraNumber < 2) {
            size_t payloadStart = i + TOTAL_HEADER_SIZE;
            size_t payloadSize = chunkTotalSize - TOTAL_HEADER_SIZE;
            
            frameBuffer.insert(frameBuffer.end(), 
                               streamBuffer.begin() + payloadStart, 
                               streamBuffer.begin() + payloadStart + payloadSize);
        }

        i += chunkTotalSize;
    }

    if (i > 0) {
        streamBuffer.erase(streamBuffer.begin(), streamBuffer.begin() + i);
    }
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