#include "buffered_mjpeg_stream.hpp"

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
#include "endian_conversion.hpp"
#include "intrusive_ptr.hpp"

BufferedMjpegStream::BufferedMjpegStream(std::shared_ptr<BufferPool> bufferPool,
                                         std::function<void(BufferPtr)> onFrameReady)
    : bufferPool_(std::move(bufferPool)), onFrameReady_(std::move(onFrameReady)) {
    inputBuffer_.reserve(Units::THIRTY_TWO_KILOBYTES);
    activeFrame_ = bufferPool_->borrow();
}

void BufferedMjpegStream::send(std::span<const uint8_t> data) {
    inputBuffer_.insert(inputBuffer_.end(), data.begin(), data.end());

    size_t i = readOffset_;

    while (i + TOTAL_USB_HEADER_SIZE <= inputBuffer_.size()) {
        up_pkt_hdr packetHeader{};
        std::memcpy(&packetHeader, &inputBuffer_[i], USB_PACKET_HEADER_SIZE);

        if (!up_is_valid_pkt_header(&packetHeader)) {
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

        if (activeFrame_ && !activeFrame_->empty() &&
            payloadHeader_.le_frame_id != payloadHeader.le_frame_id) {
            outputFrame();
        }

        payloadHeader_ = payloadHeader;

        if (up_valid_mjpeg_payload(&payloadHeader)) {
            size_t payloadStart = i + TOTAL_USB_HEADER_SIZE;
            size_t payloadSize = totalPacketSize - TOTAL_USB_HEADER_SIZE;

            if (!activeFrame_) {
                activeFrame_ = bufferPool_->borrow();
            }

            std::span<const uint8_t> toInsert(inputBuffer_.data() + payloadStart, payloadSize);
            activeFrame_->insertContent(toInsert);
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

void BufferedMjpegStream::outputFrame() {
    if (!activeFrame_ || activeFrame_->empty()) {
        return;
    }

    auto buffer = activeFrame_->getContentSlice();
    size_t soiOffset = std::string::npos;
    size_t eoiOffset = std::string::npos;

    // Scan forward for Start of Image (FF D8)
    size_t maxSoiPosition =
        std::min<size_t>(UsbProtocol::JPEG_SOI_MARKERS_MAX_POSITION, buffer.size());
    for (size_t j = 0; j + 1 < maxSoiPosition; ++j) {
        if (buffer[j] == UsbProtocol::BOUNDARY_MARKER &&
            buffer[j + 1] == UsbProtocol::START_MARKER) {
            soiOffset = j;
            break;
        }
    }

    // Scan backwards for End of Image (FF D9)
    for (size_t j = buffer.size(); j >= 2; --j) {
        if (buffer[j - 2] == UsbProtocol::BOUNDARY_MARKER &&
            buffer[j - 1] == UsbProtocol::END_MARKER) {
            eoiOffset = j;
            break;
        }
    }

    if (soiOffset != std::string::npos && eoiOffset != std::string::npos && soiOffset < eoiOffset) {
        size_t startTrim = soiOffset;
        size_t endTrim = eoiOffset;

        activeFrame_->trim(startTrim, endTrim);

        if (onFrameReady_) {
            onFrameReady_(std::move(activeFrame_));
        }
    }

    activeFrame_ = bufferPool_->borrow();
}