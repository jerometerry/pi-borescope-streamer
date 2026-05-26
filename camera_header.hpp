#pragma once

#include <cstdint>
#include <vector>

struct [[gnu::packed]] CameraHeader {
    uint8_t frameId;
    uint8_t cameraNumber;
    unsigned char hasGravitySensor:1;
    unsigned char buttonPress:1;
    unsigned char otherFlags:6;
    uint32_t gravitySensor;
};