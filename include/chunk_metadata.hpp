#pragma once

#include <cstdint>

/** 
 * @brief Structure representing the header of a camera frame
 */
struct [[gnu::packed]] ChunkMetadata {
    /** 
     * @brief The ID of the frame
     */
    uint8_t frameId;

    /** 
     * @brief The number of the camera
     */
    uint8_t cameraNumber;

    /**
     * @brief 
     */
    uint8_t flags;

    /** 
     * @brief The value of the gravity sensor
     */
    uint32_t gravitySensor;

    bool hasGravitySensor() const { 
        return (flags & 0x01) != 0; 
    }

    bool isButtonPressed() const  { 
        return (flags & 0x02) != 0; 
    }

    void setButtonPressed(bool pressed) {
        if (pressed) {
            flags |= 0x02;
        } else {
            flags &= ~0x02;
        }
    }

    uint8_t getOtherFlags() const { 
        return (flags >> 2) & 0x3F; 
    }

    void setOtherFlags(uint8_t val) {
        flags &= 0x03;
        flags |= ((val & 0x3F) << 2); 
    }
};

static_assert(sizeof(ChunkMetadata) == 7, "ChunkMetadata size must be exactly 7 bytes!");