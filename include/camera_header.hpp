#pragma once

#include <cstdint>

struct [[gnu::packed]] CameraHeader {
    uint8_t frameId;
    uint8_t cameraNumber;
    unsigned char hasGravitySensor:1;
    unsigned char buttonPress:1;
    unsigned char otherFlags:6;
    uint32_t gravitySensor;

    bool isSameCamera(CameraHeader header) const {
        return frameId == header.frameId && 
               cameraNumber == header.cameraNumber && 
               hasGravitySensor == header.hasGravitySensor && 
               otherFlags == header.otherFlags;
    }
};