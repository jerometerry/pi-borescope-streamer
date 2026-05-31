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
     * @brief Bit-mask containing 3 values
     * <p>
     * Supported features include:
     * <ul>
     *   <li>unsigned char hasGravitySensor:1;</li>
     *   <li>unsigned char buttonPress:1;</li>
     *   <li>unsigned char otherFlags:6;</li>
     * </ul>
     */
    uint8_t flags;

    /** 
     * @brief The value of the gravity sensor
     */
    uint32_t gravitySensor;

    /**
     * @brief Get hasGravitySensor bit from the flags field
     */
    bool hasGravitySensor() const { 
        return (flags & 0x01) != 0; 
    }

    /**
     * @brief Set hasGravitySensor bit in the flags field
     */
    void setHasGravitySensor(bool hasGravitySensor) { 
        if (hasGravitySensor) {
            flags |= 0x01;
        } else {
            flags &= ~0x01;
        }
    }

    /**
     * @brief Get buttonPressed bit from the flags field
     */
    bool isButtonPressed() const  { 
        return (flags & 0x02) != 0; 
    }

    /**
     * @brief Set buttonPressed bit in the flags field
     */
    void setButtonPressed(bool pressed) {
        if (pressed) {
            flags |= 0x02;
        } else {
            flags &= ~0x02;
        }
    }

    /**
     * @brief Get otherFlags value from the flags field
     */
    uint8_t getOtherFlags() const { 
        return (flags >> 2) & 0x3F; 
    }

    /**
     * @brief Set otherFlags value in the flags field
     */
    void setOtherFlags(uint8_t val) {
        flags &= 0x03;
        flags |= ((val & 0x3F) << 2); 
    }
};

static_assert(sizeof(ChunkMetadata) == 7, "ChunkMetadata size must be exactly 7 bytes!");
