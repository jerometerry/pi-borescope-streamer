#include "chunk_metadata.hpp"
#include "usb_frame_decoder.hpp"
#include "server_constants.hpp"
#include "usb_packet_header.hpp"

#include <algorithm>
#include <bit>
#include <iterator>
#include <utility>

// Define a list of valid camera IDs that we expect to receive in the USB frames. This list is used to validate the camera ID in the USB frame header and ensure that we only process frames from known camera IDs. This is important to prevent processing invalid or unexpected frames that could cause errors or unexpected behavior in our application.
static constexpr uint8_t VALID_CAMERA_IDS[] = {7, 11};

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

    if (header->header != USB_FRAME_HEADER) {
        return;
    }

    auto it = std::find(std::begin(VALID_CAMERA_IDS), std::end(VALID_CAMERA_IDS), header->cameraId);
    if (it == std::end(VALID_CAMERA_IDS)) {
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
        if (!isCameraSupported()) {
            return;
        }
    } else {
        if (!metadata_.isSameCamera(*metadata)) {
            return;
        }
    }

    if (metadata->buttonPress && buttonHandler) {
        buttonHandler();
    }

    auto cameraDataStart = data.begin() + USB_PACKET_HEADER_LENGTH + CHUNK_METADATA_LENGTH;
    auto cameraDataEnd = data.begin() + USB_PACKET_HEADER_LENGTH + header->length;
    frameBuffer.insert(frameBuffer.end(), cameraDataStart, cameraDataEnd);
}

bool UsbFrameDecoder::isCameraSupported() const {
    return metadata_.cameraNumber < 2 && metadata_.hasGravitySensor == 0 && metadata_.otherFlags == 0;
}