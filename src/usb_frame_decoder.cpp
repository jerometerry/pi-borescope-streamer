#include <algorithm>
#include <bit>
#include <iterator>
#include <utility>
#include "chunk_metadata.hpp"
#include "server_constants.hpp"
#include "usb_frame_decoder.hpp"
#include "usb_packet_header.hpp"

// Ensure that the code is compiled on a little-endian platform, since the protocol relies on little-endian byte order for the USB frame and camera header. This check is important to prevent issues with byte order when interpreting the raw byte data from the USB frames, and to ensure that the code behaves correctly on platforms with different endianness.
static_assert(std::endian::native == std::endian::little);

UsbFrameDecoder::UsbFrameDecoder(
    std::function<void(const std::vector<uint8_t>&)> broadcastHandler, std::function<void()> buttonHandler) 
    : broadcastHandler(std::move(broadcastHandler)), buttonHandler(std::move(buttonHandler)) {
        frameBuffer.reserve(ServerConstants::ONE_MEGABYTE);
    }

void UsbFrameDecoder::emitFrame() {
    if (broadcastHandler) {
        broadcastHandler(frameBuffer);
    }
    frameBuffer.clear();
}

void UsbFrameDecoder::processIncomingCameraData(std::span<const uint8_t> data) {
    if (data.size() < USB_PACKET_HEADER_LENGTH) {
        return;
    }

    const UsbPacketHeader *header = reinterpret_cast<const UsbPacketHeader *>(data.data());

    if (header->header != ServerConstants::USB_FRAME_HEADER) {
        return;
    }

    auto it = std::find(std::begin(ServerConstants::VALID_CAMERA_IDS), std::end(ServerConstants::VALID_CAMERA_IDS), header->cameraId);
    if (it == std::end(ServerConstants::VALID_CAMERA_IDS)) {
        return;
    }

    if (USB_PACKET_HEADER_LENGTH + header->length > data.size()) {
        return;
    }

    if (data.size() - USB_PACKET_HEADER_LENGTH < CHUNK_METADATA_LENGTH) {
        return;
    }

    const ChunkMetadata *metadata = reinterpret_cast<const ChunkMetadata *>(data.data() + USB_PACKET_HEADER_LENGTH);

    if (!frameBuffer.empty()) {
        if (metadata_.frameId != metadata->frameId) {
            emitFrame();
        }
    }    

    if (frameBuffer.empty()) {
        metadata_ = *metadata;
        if (!fromVideoFeed(metadata_)) {
            return;
        }
    } else {
        if (!forSameCameraAndFrame(metadata_, *metadata)) {
            return;
        }
    }

    if (metadata->isButtonPressed() && buttonHandler) {
        buttonHandler();
    }

    auto cameraDataStart = data.begin() + USB_PACKET_HEADER_LENGTH + CHUNK_METADATA_LENGTH;
    auto cameraDataEnd = data.begin() + USB_PACKET_HEADER_LENGTH + header->length;
    frameBuffer.insert(frameBuffer.end(), cameraDataStart, cameraDataEnd);
}

bool UsbFrameDecoder::fromVideoFeed(ChunkMetadata metadata) {
    return metadata.cameraNumber < 2 && !metadata.hasGravitySensor() && metadata.getOtherFlags() == 0;
}

bool UsbFrameDecoder::forSameCamera(ChunkMetadata first, ChunkMetadata second) {
    return first.cameraNumber == second.cameraNumber && 
           first.hasGravitySensor() == second.hasGravitySensor() && 
           first.getOtherFlags() == second.getOtherFlags();
}

bool UsbFrameDecoder::forSameCameraAndFrame(ChunkMetadata first, ChunkMetadata second) {
    return forSameCamera(first, second) && first.frameId == second.frameId;
}
