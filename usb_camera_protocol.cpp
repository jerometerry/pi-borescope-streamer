#include "camera_header.hpp"
#include "usb_camera_protocol.hpp"
#include "server_constants.hpp"
#include "usb_frame.hpp"

#include <bit>

static_assert(std::endian::native == std::endian::little);

UsbCameraProtocol::UsbCameraProtocol(std::function<void(const ByteVector&)> picCb, std::function<void()> btnCb) 
    : pictureCallback(std::move(picCb)), buttonCallback(std::move(btnCb)) {}

void UsbCameraProtocol::emitFrame() {
    if (pictureCallback) {
        pictureCallback(cameraBuffer);
    }
    cameraBuffer.clear();
}

void UsbCameraProtocol::handleFrame(const ByteVector &data) {
    if (data.size() < USB_HEADER_LENGTH) {
        return;
    }

    const UsbFrame *usbFrame = reinterpret_cast<const UsbFrame *>(data.data());
    if (usbFrame->magic != UPP_USB_MAGIC) {
        return;
    }
    if ((usbFrame->cameraId != UPP_CAMERA_ID_7) && (usbFrame->cameraId != UPP_CAMERA_ID_11)) {
        return;
    }
    if (USB_HEADER_LENGTH + usbFrame->length > data.size()) {
        return;
    }

    if (data.size() - USB_HEADER_LENGTH < CAMERA_HEADER_LENGTH) {
        return;
    }
    
    const CameraHeader *header = reinterpret_cast<const CameraHeader *>(data.data() + USB_HEADER_LENGTH);

    if (!cameraBuffer.empty() && cameraHeader.frameId != header->frameId) {
        emitFrame();
    }

    if (cameraBuffer.empty()) {
        cameraHeader = *header;
        if (!(
            (cameraHeader.cameraNumber < 2) && 
            (cameraHeader.hasGravitySensor == 0) && 
            (cameraHeader.otherFlags == 0)
        )) {
            return;
        }
    } else {
        if (!(
            (cameraHeader.frameId == header->frameId) && 
            (cameraHeader.cameraNumber == header->cameraNumber) && 
            (cameraHeader.hasGravitySensor == header->hasGravitySensor) && 
            (cameraHeader.otherFlags == header->otherFlags)
        )) {
            return;
        }
    }

    if (header->buttonPress && buttonCallback) {
        buttonCallback();
    }

    auto cameraDataStart = data.begin() + USB_HEADER_LENGTH + CAMERA_HEADER_LENGTH;
    auto cameraDataEnd = data.begin() + USB_HEADER_LENGTH + usbFrame->length;

    cameraBuffer.insert(cameraBuffer.end(), cameraDataStart, cameraDataEnd);
}