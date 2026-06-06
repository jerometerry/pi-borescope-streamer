#include <algorithm>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include "constants.hpp"
#include "data_structures.hpp"
#include "mjpeg_stream.hpp"

MjpegStream::MjpegStream(
    std::function<void(std::span<const uint8_t>)> onFrameReady) 
    : onFrameReady_(std::move(onFrameReady)) {
        frameBuffer_.reserve(Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);
        inputBuffer_.reserve(Units::EIGHT_KILOBYTES);
}

void MjpegStream::send(std::span<const uint8_t> data) {
    inputBuffer_.insert(
        inputBuffer_.end(), 
        data.begin(), 
        data.end()
    );

    size_t i = readOffset_;

    while (i + USB::TotalHeaderSize <= inputBuffer_.size()) {
        USB::PacketHeader packetHeader{};
        std::memcpy(&packetHeader, &inputBuffer_[i], USB::PacketHeaderSize);

        if (packetHeader.getHeader() != UsbProtocol::USB_FRAME_HEADER || 
           (packetHeader.getCameraId() != UsbProtocol::VIDEO_CAMERA_ID && 
            packetHeader.getCameraId() != UsbProtocol::GRAVITY_SENSOR_CAMERA_ID)) {
            i++;
            continue;
        }

        // Verify if a ghost header lives down the pipe *before* checking data boundaries
        bool isGhost = false;
        size_t nextHeaderOffset = 0;

        size_t maxScan = std::min<size_t>(
            UsbProtocol::MAX_SCAN_LIMIT, 
            inputBuffer_.size() - i - 3
        );

        for (size_t d = 5; d <= maxScan; ++d) {
            if (inputBuffer_[i+d] == UsbProtocol::USB_FRAME_HEADER_A && 
                inputBuffer_[i+d+1] == UsbProtocol::USB_FRAME_HEADER_B && 
                (inputBuffer_[i+d+2] == UsbProtocol::VIDEO_CAMERA_ID || 
                    inputBuffer_[i+d+2] == UsbProtocol::GRAVITY_SENSOR_CAMERA_ID)) {
                isGhost = true;
                nextHeaderOffset = d;
                break;
            }
        }

        if (isGhost) {
            i += nextHeaderOffset;
            continue;
        }

        size_t totalPacketSize = USB::PacketHeaderSize + packetHeader.getLength();

        if (i + totalPacketSize > inputBuffer_.size()) {
            break;
        }

        // if (packetHeader.getLength() < USB::PayloadHeaderSize) {
        //     i++;
        //     continue;
        // }

        USB::PayloadHeader payloadHeader{};
        std::memcpy(
            &payloadHeader, 
            &inputBuffer_[i + USB::PacketHeaderSize], 
            USB::PayloadHeaderSize
        );

        if (!frameBuffer_.empty() && payloadHeader_.getFrameId() != payloadHeader.getFrameId()) {
            outputFrame();
        }
        payloadHeader_ = payloadHeader;

        if (!payloadHeader.hasGravitySensor() && 
            payloadHeader.getOtherFlags() == 0 && 
            payloadHeader.getCameraNumber() < 2) {
            size_t payloadStart = i + USB::TotalHeaderSize;
            size_t payloadSize = totalPacketSize - USB::TotalHeaderSize;
            
            frameBuffer_.insert(
                frameBuffer_.end(), 
                inputBuffer_.begin() + payloadStart, 
                inputBuffer_.begin() + payloadStart + payloadSize
            );
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
    if (frameBuffer_.empty()) {
        return;
    }

    size_t soiOffset = std::string::npos;
    size_t eoiOffset = std::string::npos;

    // Scan forward for Start of Image (FF D8)
    size_t maxSoiPosition = std::min<size_t>(UsbProtocol::JPEG_SOI_MARKERS_MAX_POSITION, frameBuffer_.size());
    for (size_t j = 0; j + 1 < maxSoiPosition; ++j) {
        if (frameBuffer_[j] == UsbProtocol::BOUNDARY_MARKER && frameBuffer_[j+1] == UsbProtocol::START_MARKER) {
            soiOffset = j;
            break;
        }
    }

    // Scan backwards for End of Image (FF D9)
    for (size_t j = frameBuffer_.size(); j >= 2; --j) {
        if (frameBuffer_[j - 2] == UsbProtocol::BOUNDARY_MARKER && frameBuffer_[j - 1] == UsbProtocol::END_MARKER) {
            eoiOffset = j;
            break;
        }
    }

    if (soiOffset != std::string::npos && eoiOffset != std::string::npos && soiOffset < eoiOffset) {
        if (onFrameReady_) {
            onFrameReady_(std::span<const uint8_t>(
                frameBuffer_.data() + soiOffset, 
                eoiOffset - soiOffset
            ));
        }
    }
    frameBuffer_.clear();
}