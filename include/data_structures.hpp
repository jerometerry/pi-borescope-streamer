#pragma once

#include <bit>
#include <concepts>
#include <cstdint>

/**
 * @brief Safely read numbers sent by the camera, regardless of your computer's architecture.
 * @details The physical camera always sends its numbers in "Little-Endian" format (meaning 
 * it sends the least significant bytes first). If you are running this on a standard Raspberry Pi, 
 * your computer already speaks this language, and this function completely disappears when compiled, 
 * costing zero performance. 
 * 
 * However, if you compile this on a system that reads numbers backward (Big-Endian), it will 
 * automatically flip the bytes so the camera data isn't misinterpreted as corrupted garbage.
 * @param val The raw number directly from the USB wire.
 * @return The exact same number, safely formatted for your specific CPU.
 */
template <std::integral T>
constexpr T from_le(T val) noexcept {
    if constexpr (std::endian::native == std::endian::big) {
        return std::byteswap(val);
    }
    return val;
}

/**
 * @brief Safely package numbers before sending them back to the camera.
 * @details The exact reverse of `from_le`. Ensures that if we ever need to send a size 
 * or configuration number down the USB cable, the camera receives it in the Little-Endian 
 * format its hardware strictly expects.
 * @param val The native number from our software.
 * @return The number packed into the camera's required byte order.
 */
template <std::integral T>
constexpr T to_le(T val) noexcept {
    return from_le(val);
}

/**
 * @brief The "shipping label" attached to every piece of data coming from the camera.
 * @details When the camera fires data down the USB cable, it doesn't just send one massive 
 * video file. It slices the video into small chunks and slaps this exact 5-byte header 
 * on the front of each one. We read this header first to know exactly how many bytes of 
 * actual video follow it, ensuring we never read out of bounds and crash the server.
 */
struct [[gnu::packed]] UsbPacketHeader {

    /**
     * @brief The raw, un-translated secret code identifying this as a valid camera chunk.
     */
    uint16_t leHeader;

    /**
     * @brief Which physical camera lens this data came from (used if the endoscope has multiple lenses).
     */
    uint8_t cameraId;

    /**
     * @brief The raw, un-translated size of the video data attached to this label.
     */
    uint16_t leLength;

    /**
     * @brief Read the header verification code safely.
     * @return The translated verification code, ready for our software to check.
     */
    constexpr uint16_t getHeader() const noexcept { 
        return from_le(leHeader); 
    }

    /**
     * @brief Write the header verification code safely.
     * @param val The code to package for the camera.
     */
    constexpr void setHeader(uint16_t val) noexcept { 
        leHeader = to_le(val); 
    }

    /**
     * @brief Check exactly how many bytes of video data are attached to this chunk.
     * @return The safe, translated length of the upcoming data.
     */
    constexpr uint16_t getLength() const noexcept { 
        return from_le(leLength); 
    }

    /**
     * @brief Set the length of the data chunk before sending it.
     * @param val The length in bytes.
     */
    constexpr void setLength(uint16_t val) noexcept { 
        leLength = to_le(val); 
    }
};

/**
 * @brief The hidden status report embedded inside the video stream.
 * @details The physical camera handle has extra hardware, like a physical snapshot button 
 * and a gravity sensor so the app knows which way is "up". The camera sneaks this status 
 * information into the USB data stream using a tightly packed 7-byte block. This structure 
 * safely unpacks those hidden signals so our software can react when you click the hardware button.
 */
struct [[gnu::packed]] ChunkMetadata {

    /**
     * @brief A rolling counter that helps us stitch chunks together into a full picture.
     */
    uint8_t frameId;

    /**
     * @brief Identifies which lens is active on dual-lens endoscopes.
     */
    uint8_t cameraNumber;

    /**
     * @brief A densely packed byte where each bit represents a yes/no switch (like a button press).
     */
    uint8_t flags;

    /**
     * @brief The raw, un-translated orientation data from the camera's gyroscope.
     */
    uint32_t leGravitySensor;

    /**
     * @brief Get the camera's physical orientation safely.
     * @return The translated gyroscope reading.
     */
    constexpr uint32_t getGravitySensor() const noexcept { 
        return from_le(leGravitySensor); 
    }

    /**
     * @brief Set the camera's physical orientation safely.
     * @param val The gyroscope reading to pack.
     */
    constexpr void setGravitySensor(uint32_t val) noexcept { 
        leGravitySensor = to_le(val); 
    }

    /**
     * @brief Check if the camera handle actually has a gravity sensor installed.
     * @return True if the hardware supports orientation tracking.
     */
    constexpr bool hasGravitySensor() const noexcept { 
        return (flags & 0x01) != 0; 
    }

    /**
     * @brief Toggle the flag indicating if the hardware has a gravity sensor.
     * @param hasGravitySensor True to turn the flag on, false to turn it off.
     */
    constexpr void setHasGravitySensor(bool hasGravitySensor) noexcept { 
        if (hasGravitySensor) { 
            flags |= 0x01; 
        }
        else { 
            flags &= ~0x01; 
        }
    }

    /**
     * @brief Check if the user is actively pressing the physical button on the camera handle.
     * @return True if the button is currently held down.
     */
    constexpr bool isButtonPressed() const noexcept { 
        return (flags & 0x02) != 0; 
    }

    /**
     * @brief Simulate or set the state of the physical camera button.
     * @param pressed True to mark the button as pressed.
     */
    constexpr void setButtonPressed(bool pressed) noexcept {
        if (pressed) {
            flags |= 0x02;
        }
        else {
            flags &= ~0x02;
        }
    }

    /**
     * @brief Extract any extra unknown or reserved flags from the camera.
     * @return A clean byte containing only the reserved hardware flags.
     */
    constexpr uint8_t getOtherFlags() const noexcept { 
        return (flags >> 2) & 0x3F; 
    }

    /**
     * @brief Set the extra unknown or reserved hardware flags.
     * @param val The flags to pack into the remaining bits.
     */
    constexpr void setOtherFlags(uint8_t val) noexcept {
        flags &= 0x03;
        flags |= ((val & 0x3F) << 2); 
    }
};

static_assert(sizeof(ChunkMetadata) == 7, "ChunkMetadata size must be exactly 7 bytes to match the hardware protocol!");