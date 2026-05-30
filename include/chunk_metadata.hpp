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
     * @brief Flag indicating if the gravity sensor is present
     */
    unsigned char hasGravitySensor:1;

     /** 
     * @brief Flag indicating if the button is pressed
     */
    unsigned char buttonPress:1;

    /** 
     * @brief Other flags
     */
    unsigned char otherFlags:6;

    /** 
     * @brief The value of the gravity sensor
     */
    uint32_t gravitySensor;

    /** 
     * @brief Check if this camera header is for the same camera as another header
     * @param header The other camera header to compare against
     * @return true if the headers are for the same camera, false otherwise
     */
    bool isSameCamera(ChunkMetadata header) const {
        return frameId == header.frameId && 
               cameraNumber == header.cameraNumber && 
               hasGravitySensor == header.hasGravitySensor && 
               otherFlags == header.otherFlags;
    }
};