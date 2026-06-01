#pragma once

#include <bit>
#include <concepts>
#include <cstdint>

/** @brief Convert a value from little-endian to native byte order.
 *  @param val The value to convert.
 *  @return The converted value.
 */
template <std::integral T>
constexpr T from_le(T val) noexcept {
    if constexpr (std::endian::native == std::endian::big) {
        return std::byteswap(val);
    }
    return val;
}

/** @brief Convert a value from native byte order to little-endian.
 *  @param val The value to convert.
 *  @return The converted value.
 */
template <std::integral T>
constexpr T to_le(T val) noexcept {
    return from_le(val); // Swapping is symmetric
}

/** @brief A struct representing the header of a USB packet.
 */
struct [[gnu::packed]] UsbPacketHeader {

    /** @brief The header in little-endian byte order.
     */
    uint16_t header_le;

    /** @brief The camera ID.
     */
    uint8_t cameraId;
    /** @brief The length in little-endian byte order.
     */
    uint16_t length_le;

    /** @brief Get the header in native byte order.
     *  @return The header.
     */
    constexpr uint16_t getHeader() const noexcept { 
        return from_le(header_le); 
    }

    /** @brief Set the header in little-endian byte order.
     *  @param val The header to set.
     */
    constexpr void setHeader(uint16_t val) noexcept { 
        header_le = to_le(val); 
    }

    /** @brief Get the length in native byte order.
     *  @return The length.
     */
    constexpr uint16_t getLength() const noexcept { 
        return from_le(length_le); 
    }

    /** @brief Set the length in little-endian byte order.
     *  @param val The length to set.
     */
    constexpr void setLength(uint16_t val) noexcept { 
        length_le = to_le(val); 
    }
};

/** @brief A struct representing the metadata of a data chunk.
 */
struct [[gnu::packed]] ChunkMetadata {
    /** @brief The frame ID.
     */
    uint8_t frameId;

    /** @brief The camera number.
     */
    uint8_t cameraNumber;

    /** @brief The flags.
     */
    uint8_t flags;

    /** @brief The gravity sensor value in little-endian byte order.
     */
    uint32_t gravitySensor_le;

    /** @brief Get the gravity sensor value in native byte order.
     *  @return The gravity sensor value.
     */
    constexpr uint32_t getGravitySensor() const noexcept { 
        return from_le(gravitySensor_le); 
    }

    /** @brief Set the gravity sensor value in little-endian byte order.
     *  @param val The gravity sensor value to set.
     */
    constexpr void setGravitySensor(uint32_t val) noexcept { 
        gravitySensor_le = to_le(val); 
    }

    /** @brief Check if the gravity sensor is present.
     *  @return True if the gravity sensor is present, false otherwise.
     */
    constexpr bool hasGravitySensor() const noexcept { 
        return (flags & 0x01) != 0; 
    }

    /** @brief Set whether the gravity sensor is present.
     *  @param hasGravitySensor True if the gravity sensor is present, false otherwise.
     */
    constexpr void setHasGravitySensor(bool hasGravitySensor) noexcept { 
        if (hasGravitySensor) { 
            flags |= 0x01; 
        }
        else { 
            flags &= ~0x01; 
        }
    }

    /** @brief Check if the button is pressed.
     *  @return True if the button is pressed, false otherwise.
     */
    constexpr bool isButtonPressed() const noexcept { 
        return (flags & 0x02) != 0; 
    }

    /** @brief Set whether the button is pressed.
     *  @param pressed True if the button is pressed, false otherwise.
     */
    constexpr void setButtonPressed(bool pressed) noexcept {
        if (pressed) {
            flags |= 0x02;
        }
        else {
            flags &= ~0x02;
        }
    }

    /** @brief Get the other flags.
     *  @return The other flags.
     */
    constexpr uint8_t getOtherFlags() const noexcept { 
        return (flags >> 2) & 0x3F; 
    }

    /** @brief Set the other flags.
     *  @param val The other flags to set.
     */
    constexpr void setOtherFlags(uint8_t val) noexcept {
        flags &= 0x03;
        flags |= ((val & 0x3F) << 2); 
    }
};

static_assert(sizeof(ChunkMetadata) == 7, "ChunkMetadata size must be exactly 7 bytes!");