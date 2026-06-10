#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include "buffer.hpp"
#include "buffer_pool.hpp"
#include "buffer_ptr.hpp"
#include "constants.hpp"
#include "hardcore_video_frame.hpp"
#include "intrusive_ptr.hpp"
#include "mjpeg_stream.hpp"
#include "usb_packet_header.hpp"
#include "usb_payload_header.hpp"

MjpegStream::MjpegStream(FrameDisruptor& disruptor): disruptor_(&disruptor) {
    inputBuffer_.reserve(Units::THIRTY_TWO_KILOBYTES);
}

void MjpegStream::send(std::span<const uint8_t> data) {
    inputBuffer_.insert(inputBuffer_.end(), data.begin(), data.end());

    size_t i = readOffset_;

    while (i + TOTAL_USB_HEADER_SIZE <= inputBuffer_.size()) {
        UsbPacketHeader packetHeader{};
        std::memcpy(&packetHeader, &inputBuffer_[i], USB_PACKET_HEADER_SIZE);

        if (packetHeader.getHeader() != UsbProtocol::USB_FRAME_HEADER || 
           (packetHeader.getCameraId() != UsbProtocol::VIDEO_CAMERA_ID && 
            packetHeader.getCameraId() != UsbProtocol::GRAVITY_SENSOR_CAMERA_ID)) {
            i++;
            continue;
        }

        bool isGhost = false;
        size_t nextHeaderOffset = 0;
        size_t maxScan = std::min<size_t>(
            UsbProtocol::MAX_SCAN_LIMIT,
            inputBuffer_.size() - i - 3
        );

        for (size_t d = USB_PACKET_HEADER_SIZE; d <= maxScan; ++d) {
            if (inputBuffer_[i+d] == UsbProtocol::USB_FRAME_HEADER_A && 
                inputBuffer_[i+d+1] == UsbProtocol::USB_FRAME_HEADER_B && (
                    inputBuffer_[i+d+2] == UsbProtocol::VIDEO_CAMERA_ID || 
                    inputBuffer_[i+d+2] == UsbProtocol::GRAVITY_SENSOR_CAMERA_ID
                )
            ) {
                isGhost = true;
                nextHeaderOffset = d;
                break;
            }
        }

        if (isGhost) {
            i += nextHeaderOffset;
            continue;
        }

        size_t totalPacketSize = USB_PACKET_HEADER_SIZE + packetHeader.getLength();

        if (i + totalPacketSize > inputBuffer_.size()) { 
            break;
        }

        if (packetHeader.getLength() < USB_PAYLOAD_HEADER_SIZE) {
            i++;
            continue;
        }

        UsbPayloadHeader payloadHeader{};
        std::memcpy(
            &payloadHeader,
            &inputBuffer_[i + USB_PACKET_HEADER_SIZE],
             USB_PAYLOAD_HEADER_SIZE
        );

        if (frameActive_ && 
            disruptor_->get_by_sequence(current_claim_sqe_).active_size> 0 && 
            payloadHeader_.getFrameId() != payloadHeader.getFrameId()) {
            outputFrame();
        }
        
        payloadHeader_ = payloadHeader;

        if (!payloadHeader.hasGravitySensor() && 
            payloadHeader.getOtherFlags() == 0 && 
            payloadHeader.getCameraNumber() < 2) {

            size_t payloadStart = i + TOTAL_USB_HEADER_SIZE;
            size_t payloadSize = totalPacketSize - TOTAL_USB_HEADER_SIZE;

            HardcoreVideoFrame& slot = getActiveFrameSlot();

            std::span<const uint8_t> toInsert(
                inputBuffer_.data() + payloadStart, 
                payloadSize
            );
            slot.append_payload(toInsert);
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

    HardcoreVideoFrame& slot = disruptor_->get_by_sequence(current_claim_sqe_);
    if (slot.active_size == 0) {
        frameActive_ = false;
        return;
    }

    std::span<const uint8_t> buffer(slot.storage.data(), slot.active_size);
    size_t soiOffset = std::string::npos;
    size_t eoiOffset = std::string::npos;

    // Scan forward for Start of Image (FF D8)
    size_t maxSoiPosition = std::min<size_t>(UsbProtocol::JPEG_SOI_MARKERS_MAX_POSITION, buffer.size());
    for (size_t j = 0; j + 1 < maxSoiPosition; ++j) {
        if (buffer[j] == UsbProtocol::BOUNDARY_MARKER && buffer[j+1] == UsbProtocol::START_MARKER) {
            soiOffset = j;
            break;
        }
    }

    // Scan backwards for End of Image (FF D9)
    for (size_t j = buffer.size(); j >= 2; --j) {
        if (buffer[j - 2] == UsbProtocol::BOUNDARY_MARKER && buffer[j - 1] == UsbProtocol::END_MARKER) {
            eoiOffset = j;
            break;
        }
    }

    if (soiOffset != std::string::npos && eoiOffset != std::string::npos && soiOffset < eoiOffset) {
        size_t startTrim = soiOffset;
        size_t endTrim = eoiOffset;

        slot.trim(startTrim, endTrim);


        disruptor_->publish(current_claim_sqe_);
    }

    frameActive_ = false;
}

HardcoreVideoFrame& MjpegStream::getActiveFrameSlot() {
    if (!frameActive_) {
        current_claim_sqe_ = disruptor_->claim();
        HardcoreVideoFrame& slot = disruptor_->get_by_sequence(current_claim_sqe_);
        slot.clear();
        frameActive_ = true;
        return slot;
    }
    return disruptor_->get_by_sequence(current_claim_sqe_);
}
    