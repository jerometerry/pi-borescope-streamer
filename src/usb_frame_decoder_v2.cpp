#include "usb_frame_decoder_v2.hpp"
#include "usb_packet_header.hpp"
#include "chunk_metadata.hpp"
#include "server_constants.hpp"
#include "server_constants.hpp"

#include <algorithm>
#include <bit>

// Define a list of valid camera IDs that we expect to receive in the USB frames. This list is used to validate the camera ID in the USB frame header and ensure that we only process frames from known camera IDs. This is important to prevent processing invalid or unexpected frames that could cause errors or unexpected behavior in our application.
static constexpr uint8_t VALID_CAMERA_IDS[] = {7, 11};

// Ensure that the code is compiled on a little-endian platform, since the protocol relies on little-endian byte order for the USB frame and camera header. This check is important to prevent issues with byte order when interpreting the raw byte data from the USB frames, and to ensure that the code behaves correctly on platforms with different endianness.
static_assert(std::endian::native == std::endian::little);

UsbFrameDecoderV2::UsbFrameDecoderV2(
    std::function<void(const std::vector<uint8_t>&)> broadcastHandler, std::function<void()> buttonHandler) 
    : broadcastHandler(std::move(broadcastHandler)), buttonHandler(std::move(buttonHandler)) {
        frameBuffer.reserve(ServerConstants::ONE_MEGABYTE);
    }

void UsbFrameDecoderV2::emitFrame() {
    if (broadcastHandler) {
        broadcastHandler(frameBuffer);
    }
    frameBuffer.clear();
}

void UsbFrameDecoderV2::processIncomingCameraData(std::span<const uint8_t> data) {
    size_t offset = 0;

    // Continue as long as there are enough bytes left to form a valid routing header
    while (offset + USB_FRAME_LENGTH <= data.size()) {
        
        const UsbPacketHeader *usbFrame = reinterpret_cast<const UsbPacketHeader *>(data.data() + offset);

        // 1. SLIDING WINDOW SYNC: If we lose the header due to electrical noise, 
        // advance byte-by-byte until we find the next 0xBBAA signature.
        if (usbFrame->header != USB_FRAME_HEADER) {
            offset++;
            continue;
        }

        // Validate Camera ID
        auto it = std::find(std::begin(VALID_CAMERA_IDS), std::end(VALID_CAMERA_IDS), usbFrame->cameraId);
        if (it == std::end(VALID_CAMERA_IDS)) {
            offset++; // Invalid ID. Assume corruption and try to resync.
            continue;
        }

        size_t totalChunkSize = USB_FRAME_LENGTH + usbFrame->length;

        // 2. BOUNDARY CHECK: If libusb cut the final packet in half at the end of our 4KB buffer,
        // break and wait for the remaining bytes in the next read loop.
        if (offset + totalChunkSize > data.size()) {
            break;
        }

        // Validate payload length
        if (usbFrame->length < CAMERA_FRAME_LENGTH) {
            offset += totalChunkSize;
            continue;
        }

        const ChunkMetadata *frame = reinterpret_cast<const ChunkMetadata *>(data.data() + offset + USB_FRAME_LENGTH);

        // 3. EMIT FRAME: If the frame ID changes, we finished the last JPEG
        if (!frameBuffer.empty() && cameraFrame.frameId != frame->frameId) {
            emitFrame();
        }

        // 4. HARDWARE STATE: Track the active camera
        if (frameBuffer.empty()) {
            cameraFrame = *frame;
            if (!isCameraSupported()) {
                offset += totalChunkSize;
                continue;
            }
        } else {
            if (!cameraFrame.isSameCamera(*frame)) {
                offset += totalChunkSize;
                continue;
            }
        }

        // 5. HARDWARE INTERRUPT: Trigger the snapshot if button is clicked
        if (frame->buttonPress && buttonHandler) {
            buttonHandler();
        }

        // 6. ACCUMULATION: Strip the headers and append ONLY the JPEG pixels
        auto payloadStart = data.begin() + offset + USB_FRAME_LENGTH + CAMERA_FRAME_LENGTH;
        auto payloadEnd   = data.begin() + offset + totalChunkSize;
        frameBuffer.insert(frameBuffer.end(), payloadStart, payloadEnd);

        // 7. ADVANCE: Move the offset pointer perfectly to the start of the next packet in the span
        offset += totalChunkSize;
    }
}

bool UsbFrameDecoderV2::isCameraSupported() const {
    return cameraFrame.cameraNumber < 2 && cameraFrame.hasGravitySensor == 0 && cameraFrame.otherFlags == 0;
}