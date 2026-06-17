#include "mjpeg_stream.hpp"
extern "C" {
#include "useeplus_protocol.h"
}

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "constants.hpp"
#include "endian_conversion.hpp"
#include "video_frame.hpp"
#include "video_frame_buffer.hpp"

MjpegStream::MjpegStream(VideoFrameBuffer& disruptor) : disruptor_(&disruptor) {
    inputBuffer_.reserve(Units::THIRTY_TWO_KILOBYTES);
}

void MjpegStream::send(std::span<const uint8_t> data) {
    inputBuffer_.insert(inputBuffer_.end(), data.begin(), data.end());

    size_t i = readOffset_;

    while (i + TOTAL_USB_HEADER_SIZE <= inputBuffer_.size()) {
        up_pkt_hdr packetHeader{};
        std::memcpy(&packetHeader, &inputBuffer_[i], USB_PACKET_HEADER_SIZE);

        if (!up_is_valid_pkt_hdr(&packetHeader)) {
            i++;
            continue;
        }

        bool isGhost = false;
        size_t nextHeaderOffset = 0;
        size_t maxScan = std::min<size_t>(UsbProtocol::MAX_SCAN_LIMIT, inputBuffer_.size() - i - 3);

        for (size_t d = USB_PACKET_HEADER_SIZE; d <= maxScan; ++d) {
            if (inputBuffer_[i + d] == UsbProtocol::USB_FRAME_HEADER_A &&
                inputBuffer_[i + d + 1] == UsbProtocol::USB_FRAME_HEADER_B &&
                (inputBuffer_[i + d + 2] == UsbProtocol::VIDEO_CAMERA_ID ||
                 inputBuffer_[i + d + 2] == UsbProtocol::GRAVITY_SENSOR_CAMERA_ID)) {
                isGhost = true;
                nextHeaderOffset = d;
                break;
            }
        }

        if (isGhost) {
            i += nextHeaderOffset;
            continue;
        }

        size_t packetLength = EndianConversion::wireToHost(packetHeader.le_length);
        size_t totalPacketSize = USB_PACKET_HEADER_SIZE + packetLength;

        if (i + totalPacketSize > inputBuffer_.size()) {
            break;
        }

        if (packetLength < USB_PAYLOAD_HEADER_SIZE) {
            i++;
            continue;
        }

        up_pl_hdr payloadHeader{};
        std::memcpy(&payloadHeader, &inputBuffer_[i + USB_PACKET_HEADER_SIZE],
                    USB_PAYLOAD_HEADER_SIZE);

        if (frameActive_ && disruptor_->getBySequence(currentClaimSequence_).contentSize() > 0 &&
            payloadHeader_.le_frame_id != payloadHeader.le_frame_id) {
            outputFrame();
        }

        payloadHeader_ = payloadHeader;

        if (up_valid_mjpeg_payload(&payloadHeader)) {
            size_t payloadStart = i + TOTAL_USB_HEADER_SIZE;
            size_t payloadSize = totalPacketSize - TOTAL_USB_HEADER_SIZE;

            VideoFrame& slot = getActiveFrameSlot();

            std::span<const uint8_t> toInsert(inputBuffer_.data() + payloadStart, payloadSize);
            slot.insertContent(toInsert);
        }

        i += totalPacketSize;
    }

    readOffset_ = i;

    if (readOffset_ == inputBuffer_.size()) {
        inputBuffer_.clear();
        readOffset_ = 0;
    } else if (readOffset_ > Units::FOUR_KILOBYTES) {
        inputBuffer_.erase(inputBuffer_.begin(), inputBuffer_.begin() + readOffset_);
        readOffset_ = 0;
    }
}

void MjpegStream::outputFrame() {
    if (!frameActive_) {
        return;
    }

    VideoFrame& slot = disruptor_->getBySequence(currentClaimSequence_);

    if (slot.contentSize() == 0) {
        disruptor_->publish(currentClaimSequence_);
        frameActive_ = false;
        return;
    }

    size_t soiOffset = std::string::npos;
    size_t eoiOffset = std::string::npos;

    // Scan forward for Start of Image (FF D8)
    size_t maxSoiPosition =
        std::min<size_t>(UsbProtocol::JPEG_SOI_MARKERS_MAX_POSITION, slot.contentSize());
    for (size_t j = 0; j + 1 < maxSoiPosition; ++j) {
        if (slot.storage[slot.paddingSize() + j] == UsbProtocol::BOUNDARY_MARKER &&
            slot.storage[slot.paddingSize() + j + 1] == UsbProtocol::START_MARKER) {
            soiOffset = j;
            break;
        }
    }

    // Scan backwards for End of Image (FF D9)
    for (size_t j = slot.contentSize(); j >= 2; --j) {
        if (slot.storage[slot.paddingSize() + j - 2] == UsbProtocol::BOUNDARY_MARKER &&
            slot.storage[slot.paddingSize() + j - 1] == UsbProtocol::END_MARKER) {
            eoiOffset = j;
            break;
        }
    }

    if (soiOffset != std::string::npos && eoiOffset != std::string::npos && soiOffset < eoiOffset) {
        size_t startTrim = soiOffset;
        size_t endTrim = eoiOffset;
        slot.trim(startTrim, endTrim);
    } else {
        slot.clear();
    }

    disruptor_->publish(currentClaimSequence_);
    frameActive_ = false;
}

VideoFrame& MjpegStream::getActiveFrameSlot() {
    if (!frameActive_) {
        currentClaimSequence_ = disruptor_->claim();
        VideoFrame& slot = disruptor_->getBySequence(currentClaimSequence_);
        slot.clear();
        frameActive_ = true;
        return slot;
    }
    return disruptor_->getBySequence(currentClaimSequence_);
}
