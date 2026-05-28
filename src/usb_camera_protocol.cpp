#include "camera_header.hpp"
#include "usb_camera_protocol.hpp"
#include "server_constants.hpp"
#include "usb_frame.hpp"

#include <algorithm>
#include <bit>

// Define a list of valid camera IDs that we expect to receive in the USB frames. This list is used to validate the camera ID in the USB frame header and ensure that we only process frames from known camera IDs. This is important to prevent processing invalid or unexpected frames that could cause errors or unexpected behavior in our application.
static uint8_t VALID_CAMERA_IDS[] = {7, 11};

// Ensure that the code is compiled on a little-endian platform, since the protocol relies on little-endian byte order for the USB frame and camera header. This check is important to prevent issues with byte order when interpreting the raw byte data from the USB frames, and to ensure that the code behaves correctly on platforms with different endianness.
static_assert(std::endian::native == std::endian::little);

UsbCameraProtocol::UsbCameraProtocol(
    std::function<void(const std::vector<uint8_t>&)> broadcastHandler, std::function<void()> buttonHandler) 
    : broadcastHandler(std::move(broadcastHandler)), buttonHandler(std::move(buttonHandler)) {
        frameBuffer.reserve(ServerConstants::ONE_MEGABYTE);
    }

void UsbCameraProtocol::emitFrame() {
    if (broadcastHandler) {
        broadcastHandler(frameBuffer);
    }
    frameBuffer.clear();
}

void UsbCameraProtocol::handleFrame(const std::vector<uint8_t> &frame) {
    if (frame.size() < USB_HEADER_LENGTH) {
        return;
    }

    const UsbFrame *usbFrame = reinterpret_cast<const UsbFrame *>(frame.data());

    if (usbFrame->header != USB_FRAME_HEADER) {
        return;
    }

    auto it = std::find(std::begin(VALID_CAMERA_IDS), std::end(VALID_CAMERA_IDS), usbFrame->cameraId);
    if (it == std::end(VALID_CAMERA_IDS)) {
        return;
    }

    if (USB_HEADER_LENGTH + usbFrame->length > frame.size()) {
        return;
    }

    if (frame.size() - USB_HEADER_LENGTH < CAMERA_HEADER_LENGTH) {
        return;
    }

    const CameraHeader *header = reinterpret_cast<const CameraHeader *>(frame.data() + USB_HEADER_LENGTH);

    // If the frame ID changes, emit the current frame buffer before processing the new frame
    if (!frameBuffer.empty() && cameraHeader.frameId != header->frameId) {
        emitFrame();
    }

    if (frameBuffer.empty()) {
        cameraHeader = *header;
        if (!isCameraSupported()) {
            return;
        }
    } else {
        if (!cameraHeader.isSameCamera(*header)) {
            return;
        }
    }

    // If the button press flag is set, call the button handler
    if (header->buttonPress && buttonHandler) {
        buttonHandler();
    }

    // Append the camera data to the frame buffer
    auto cameraDataStart = frame.begin() + USB_HEADER_LENGTH + CAMERA_HEADER_LENGTH;
    auto cameraDataEnd = frame.begin() + USB_HEADER_LENGTH + usbFrame->length;
    frameBuffer.insert(frameBuffer.end(), cameraDataStart, cameraDataEnd);
}

bool UsbCameraProtocol::isCameraSupported() {
    return cameraHeader.cameraNumber < 2 && cameraHeader.hasGravitySensor == 0 && cameraHeader.otherFlags == 0;
}