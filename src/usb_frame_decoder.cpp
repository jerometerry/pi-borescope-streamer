#include "usb_frame_decoder.hpp"
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

void UsbFrameDecoder::processIncomingCameraData(std::span<const uint8_t> readBuffer) {
    if (readBuffer.size() < USB_FRAME_LENGTH) {
        return;
    }

    const UsbPacketHeader *usbFrame = reinterpret_cast<const UsbPacketHeader *>(readBuffer.data());

    if (usbFrame->header != USB_FRAME_HEADER) {
        return;
    }

    auto it = std::find(std::begin(VALID_CAMERA_IDS), std::end(VALID_CAMERA_IDS), usbFrame->cameraId);
    if (it == std::end(VALID_CAMERA_IDS)) {
        return;
    }

    if (USB_FRAME_LENGTH + usbFrame->length > readBuffer.size()) {
        return;
    }

    if (readBuffer.size() - USB_FRAME_LENGTH < CAMERA_FRAME_LENGTH) {
        return;
    }

    const ChunkMetadata *frame = reinterpret_cast<const ChunkMetadata *>(readBuffer.data() + USB_FRAME_LENGTH);

    // If the frame ID changes, emit the current frame buffer before processing the new frame
    if (!frameBuffer.empty() && cameraFrame.frameId != frame->frameId) {
        emitFrame();
    }

    if (frameBuffer.empty()) {
        cameraFrame = *frame;
        if (!isCameraSupported()) {
            return;
        }
    } else {
        if (!cameraFrame.isSameCamera(*frame)) {
            return;
        }
    }

    // If the button press flag is set, call the button handler
    if (frame->buttonPress && buttonHandler) {
        buttonHandler();
    }

    // Append the camera data to the frame buffer
    auto cameraDataStart = readBuffer.begin() + USB_FRAME_LENGTH + CAMERA_FRAME_LENGTH;
    auto cameraDataEnd = readBuffer.begin() + USB_FRAME_LENGTH + usbFrame->length;
    frameBuffer.insert(frameBuffer.end(), cameraDataStart, cameraDataEnd);
}

bool UsbFrameDecoder::isCameraSupported() const {
    return cameraFrame.cameraNumber < 2 && cameraFrame.hasGravitySensor == 0 && cameraFrame.otherFlags == 0;
}