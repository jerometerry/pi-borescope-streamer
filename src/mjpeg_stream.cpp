#include <algorithm>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include "mjpeg_stream.hpp"
#include "server_constants.hpp"

MjpegStream::MjpegStream(
    std::function<void(const std::vector<uint8_t>&)> output) 
    : output_(std::move(output)) {
        frameBuffer_.reserve(ServerConstants::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);
        streamBuffer_.reserve(ServerConstants::EIGHT_KILOBYTES);
        emitBuffer_.reserve(ServerConstants::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);
}

void MjpegStream::send(std::span<const uint8_t> data) {
    streamBuffer_.insert(streamBuffer_.end(), data.begin(), data.end());

    size_t i = readOffset_;
    const size_t TOTAL_HEADER_SIZE = sizeof(UsbPacketHeader) + sizeof(CameraPacketHeader);

    while (i + TOTAL_HEADER_SIZE <= streamBuffer_.size()) {
        UsbPacketHeader header{};
        std::memcpy(&header, &streamBuffer_[i], sizeof(UsbPacketHeader));

        if (header.getHeader() != ServerConstants::USB_FRAME_HEADER || 
           (header.getCameraId() != 0x0B && header.getCameraId() != 0x07)) {
            i++;
            continue;
        }

        // Verify if a ghost header lives down the pipe *before* checking data boundaries
        bool isGhost = false;
        size_t nextHeaderOffset = 0;

        size_t maxScan = std::min<size_t>(300, streamBuffer_.size() - i - 3); 
        for (size_t d = 5; d <= maxScan; ++d) {
            if (streamBuffer_[i+d] == 0xAA && streamBuffer_[i+d+1] == 0xBB && 
               (streamBuffer_[i+d+2] == 0x0B || streamBuffer_[i+d+2] == 0x07)) {
                isGhost = true;
                nextHeaderOffset = d;
                break;
            }
        }

        if (isGhost) {
            i += nextHeaderOffset;
            continue;
        }

        size_t chunkTotalSize = sizeof(UsbPacketHeader) + header.getLength();

        if (i + chunkTotalSize > streamBuffer_.size()) {
            break;
        }

        CameraPacketHeader meta{};
        std::memcpy(&meta, &streamBuffer_[i + sizeof(UsbPacketHeader)], sizeof(CameraPacketHeader));

        if (!frameBuffer_.empty() && metadata_.getFrameId() != meta.getFrameId()) {
            trimAndEmitFrame();
        }
        metadata_ = meta;

        if (!meta.hasGravitySensor() && meta.getOtherFlags() == 0 && meta.getCameraNumber() < 2) {
            size_t payloadStart = i + TOTAL_HEADER_SIZE;
            size_t payloadSize = chunkTotalSize - TOTAL_HEADER_SIZE;
            
            frameBuffer_.insert(frameBuffer_.end(), 
                               streamBuffer_.begin() + payloadStart, 
                               streamBuffer_.begin() + payloadStart + payloadSize);
        }

        i += chunkTotalSize;
    }

    readOffset_ = i;

    if (readOffset_ == streamBuffer_.size()) {
        streamBuffer_.clear();
        readOffset_ = 0;
    } else if (readOffset_ > ServerConstants::FOUR_KILOBYTES) {
        streamBuffer_.erase(streamBuffer_.begin(), streamBuffer_.begin() + readOffset_);
        readOffset_ = 0;
    }
}

void MjpegStream::trimAndEmitFrame() {
    if (frameBuffer_.empty()) {
        return;
    }

    size_t soiOffset = std::string::npos;
    size_t eoiOffset = std::string::npos;

    // Scan forward for Start of Image (FF D8)
    for (size_t j = 0; j + 1 < std::min<size_t>(256, frameBuffer_.size()); ++j) {
        if (frameBuffer_[j] == 0xFF && frameBuffer_[j+1] == 0xD8) {
            soiOffset = j;
            break;
        }
    }

    // Scan backwards for End of Image (FF D9)
    for (size_t j = frameBuffer_.size(); j >= 2; --j) {
        if (frameBuffer_[j - 2] == 0xFF && frameBuffer_[j - 1] == 0xD9) {
            eoiOffset = j;
            break;
        }
    }

    // If we have valid boundaries, slice the pure JPEG and fire it off
    if (soiOffset != std::string::npos && eoiOffset != std::string::npos && soiOffset < eoiOffset) {
        size_t frameSize = eoiOffset - soiOffset;
        emitBuffer_.clear();

        if (emitBuffer_.capacity() < frameSize) {
            emitBuffer_.reserve(frameSize); 
        }

        emitBuffer_.insert(emitBuffer_.end(), frameBuffer_.begin() + soiOffset, frameBuffer_.begin() + eoiOffset);

        if (output_) {
            output_(emitBuffer_);
        }
    }
    
    frameBuffer_.clear();
}