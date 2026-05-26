#include "borescope_stream_decoder.hpp"
#include "server_constants.hpp"
#include "usb_frame.hpp"
#include "camera_header.hpp"
#include <bit>

static_assert(std::endian::native == std::endian::little);

BorescopeStreamDecoder::BorescopeStreamDecoder(std::function<void(const byteVector&)> picCb, std::function<void()> btnCb) 
    : pictureCallback(std::move(picCb)), buttonCallback(std::move(btnCb)) {}

void BorescopeStreamDecoder::emitFrame() {
    if (pictureCallback) {
        pictureCallback(cameraBuffer);
    }
    cameraBuffer.clear();
}

void BorescopeStreamDecoder::handleUseeplusFrame(const byteVector &data) {
    size_t usbHeaderLength = sizeof(UsbFrame);
    if (data.size() < usbHeaderLength) {
        return;
    }

    const UsbFrame *usbFrame = reinterpret_cast<const UsbFrame *>(data.data());
    if (usbFrame->magic != UPP_USB_MAGIC) {
        return;
    }
    if ((usbFrame->cameraId != UPP_CAMERA_ID_7) && (usbFrame->cameraId != UPP_CAMERA_ID_11)) {
        return;
    }
    if (usbHeaderLength + usbFrame->length > data.size()) {
        return;
    }

    size_t cameraHeaderLength = sizeof(CameraHeader);
    if (data.size() - usbHeaderLength < cameraHeaderLength) {
        return;
    }
    
    const CameraHeader *parsedCameraHeader = reinterpret_cast<const CameraHeader *>(data.data() + usbHeaderLength);

    if (!cameraBuffer.empty() && cameraHeader.frameId != parsedCameraHeader->frameId) {
        emitFrame();
    }

    if (cameraBuffer.empty()) {
        cameraHeader = *parsedCameraHeader;
        if (!((cameraHeader.cameraNumber < 2) && (cameraHeader.hasGravitySensor == 0) && (cameraHeader.otherFlags == 0))) {
            return;
        }
    } else {
        if (!((cameraHeader.frameId == parsedCameraHeader->frameId) && 
              (cameraHeader.cameraNumber == parsedCameraHeader->cameraNumber) && 
              (cameraHeader.hasGravitySensor == parsedCameraHeader->hasGravitySensor) && 
              (cameraHeader.otherFlags == parsedCameraHeader->otherFlags))) {
            return;
        }
    }

    if (parsedCameraHeader->buttonPress && buttonCallback) {
        buttonCallback();
    }

    auto cameraDataStart = data.begin() + usbHeaderLength + cameraHeaderLength;
    auto cameraDataEnd = data.begin() + usbHeaderLength + usbFrame->length;
    cameraBuffer.insert(cameraBuffer.end(), cameraDataStart, cameraDataEnd);
}