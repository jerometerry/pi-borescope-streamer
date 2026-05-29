#pragma once

#include <cstdint>

/** 
 * @brief Structure representing a USB frame
 */
struct [[gnu::packed]] UsbFrame {

    /** @brief The header of the USB frame */
    uint16_t header;

    /** @brief The ID of the camera that generated the frame */
    uint8_t cameraId;

    /** @brief The length of the USB frame */
    uint16_t length;
};