#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include "typedefs.hpp"

class BorescopeStreamDecoder {
private:
    static constexpr uint16_t UPP_USB_MAGIC = 0xBBAA;
    static constexpr uint8_t UPP_CAMERA_ID_7 = 7;
    static constexpr uint8_t UPP_CAMERA_ID_11 = 11;

    byteVector cameraBuffer;
    upp_cam_frame_t cameraHeader{};
    
    std::function<void(const byteVector&)> pictureCallback;
    std::function<void()> buttonCallback;

    void emitFrame();

public:
    explicit BorescopeStreamDecoder(
        std::function<void(const byteVector&)> pictureCallback, 
        std::function<void()> buttonCallback
    );

    void handleUseeplusFrame(const byteVector &data);
};