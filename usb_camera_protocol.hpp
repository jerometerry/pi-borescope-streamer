#pragma once

#include "camera_header.hpp"
#include "usb_frame.hpp"

#include <cstdint>
#include <functional>

class UsbCameraProtocol {
public:
    explicit UsbCameraProtocol(
        std::function<void(const std::vector<uint8_t>&)> broadcastHandler, 
        std::function<void()> buttonHandler
    );

    void handleFrame(const std::vector<uint8_t> &frame);

private:
    static constexpr uint16_t USB_FRAME_HEADER = 0xBBAA;
    static constexpr size_t USB_HEADER_LENGTH = sizeof(UsbFrame);
    static constexpr size_t CAMERA_HEADER_LENGTH = sizeof(CameraHeader);

    std::vector<uint8_t> frameBuffer;
    CameraHeader cameraHeader{};
    
    std::function<void(const std::vector<uint8_t>&)> broadcastHandler;
    std::function<void()> buttonHandler;

    void emitFrame();

    bool isCameraSupported();
};