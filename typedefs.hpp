#pragma once

#include <cstdint>
#include <vector>

using byteVector = std::vector<uint8_t>;
using vid_pid_t = std::pair<uint16_t, uint16_t>;

struct [[gnu::packed]] upp_usb_frame_t {
    uint16_t magic;
    uint8_t cameraId;
    uint16_t length; 
};

struct [[gnu::packed]] upp_cam_frame_t {
    uint8_t frameId;
    uint8_t cameraNumber;
    unsigned char hasGravitySensor:1;
    unsigned char buttonPress:1;
    unsigned char otherFlags:6;
    uint32_t gravitySensor;
};