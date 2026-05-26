#pragma once

#include <cstdint>
#include <vector>

using byteVector = std::vector<uint8_t>;
using vid_pid_t = std::pair<uint16_t, uint16_t>;

struct [[gnu::packed]] UsbFrame {
    uint16_t magic;
    uint8_t cameraId;
    uint16_t length; 
};