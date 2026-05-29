#pragma once

#include <cstdint>

struct [[gnu::packed]] UsbFrame {
    uint16_t header;
    uint8_t cameraId;
    uint16_t length;
};