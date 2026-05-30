#pragma once

#include "chunk_metadata.hpp"
#include "usb_frame.hpp"

#include <cstdint>
#include <functional>

class UsbFrameDecoder {
public:
    explicit UsbFrameDecoder(
        std::function<void(const std::vector<uint8_t>&)> broadcastHandler, 
        std::function<void()> buttonHandler
    );

    void processIncomingCameraData(const std::vector<uint8_t> &data);

private:
    static constexpr uint16_t USB_FRAME_HEADER = 0xBBAA;
    static constexpr size_t USB_HEADER_LENGTH = sizeof(UsbFrame);
    static constexpr size_t CAMERA_HEADER_LENGTH = sizeof(ChunkMetadata);

    std::vector<uint8_t> frameBuffer;
    ChunkMetadata cameraHeader{};
    
    std::function<void(const std::vector<uint8_t>&)> broadcastHandler;
    std::function<void()> buttonHandler;

    void emitFrame();

    bool isCameraSupported() const;
};