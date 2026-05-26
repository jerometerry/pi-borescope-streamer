#pragma once

#include <cstdint>

struct [[gnu::packed]] UsbFrame {
    uint16_t magic;
    uint8_t cameraId;
    uint16_t length; 
};