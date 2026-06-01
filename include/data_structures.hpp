#pragma once

#include <bit>
#include <concepts>
#include <cstdint>

template <std::integral T>
constexpr T from_le(T val) noexcept {
    if constexpr (std::endian::native == std::endian::big) {
        return std::byteswap(val);
    }
    return val;
}

template <std::integral T>
constexpr T to_le(T val) noexcept {
    return from_le(val); // Swapping is symmetric
}

struct [[gnu::packed]] UsbPacketHeader {
    uint16_t header_le;
    uint8_t cameraId;
    uint16_t length_le;

    constexpr uint16_t getHeader() const noexcept { return from_le(header_le); }
    constexpr void setHeader(uint16_t val) noexcept { header_le = to_le(val); }

    constexpr uint16_t getLength() const noexcept { return from_le(length_le); }
    constexpr void setLength(uint16_t val) noexcept { length_le = to_le(val); }
};

struct [[gnu::packed]] ChunkMetadata {
    uint8_t frameId;
    uint8_t cameraNumber;
    uint8_t flags;
    uint32_t gravitySensor_le;

    constexpr uint32_t getGravitySensor() const noexcept { return from_le(gravitySensor_le); }
    constexpr void setGravitySensor(uint32_t val) noexcept { gravitySensor_le = to_le(val); }

    constexpr bool hasGravitySensor() const noexcept { return (flags & 0x01) != 0; }
    constexpr void setHasGravitySensor(bool hasGravitySensor) noexcept { 
        if (hasGravitySensor) flags |= 0x01;
        else flags &= ~0x01;
    }

    constexpr bool isButtonPressed() const noexcept { return (flags & 0x02) != 0; }
    constexpr void setButtonPressed(bool pressed) noexcept {
        if (pressed) flags |= 0x02;
        else flags &= ~0x02;
    }

    constexpr uint8_t getOtherFlags() const noexcept { return (flags >> 2) & 0x3F; }
    constexpr void setOtherFlags(uint8_t val) noexcept {
        flags &= 0x03;
        flags |= ((val & 0x3F) << 2); 
    }
};

static_assert(sizeof(ChunkMetadata) == 7, "ChunkMetadata size must be exactly 7 bytes!");