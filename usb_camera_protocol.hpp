#pragma once

#include "camera_header.hpp"
#include "usb_frame.hpp"
#include "typedefs.hpp"

#include <cstdint>
#include <functional>

class UsbCameraProtocol {
private:
    static constexpr uint16_t UPP_USB_MAGIC = 0xBBAA;
    static constexpr uint8_t UPP_CAMERA_ID_7 = 7;
    static constexpr uint8_t UPP_CAMERA_ID_11 = 11;
    static constexpr size_t USB_HEADER_LENGTH = sizeof(UsbFrame);
    static constexpr size_t CAMERA_HEADER_LENGTH = sizeof(CameraHeader);

    ByteVector cameraBuffer;
    CameraHeader cameraHeader{};
    
    std::function<void(const ByteVector&)> pictureCallback;
    std::function<void()> buttonCallback;

    void emitFrame();

public:
    explicit UsbCameraProtocol(
        std::function<void(const ByteVector&)> pictureCallback, 
        std::function<void()> buttonCallback
    );

    void handleFrame(const ByteVector &data);
};