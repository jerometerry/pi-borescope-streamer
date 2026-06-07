#pragma once

#include <atomic>
#include <bit>
#include <concepts>
#include <cstdint>
#include <vector>

namespace USB {
	/**
    * @brief Single-byte bypass for wire translation.
    * @details A single byte (8 bits) has no endianness. This overload ensures that 
    * asking to translate a single byte safely returns the exact same value without 
    * invoking the template logic.
    * @param val The raw byte directly from the USB wire.
    * @return The exact same byte.
    */
    constexpr uint8_t wireToHost(uint8_t val) noexcept {
        return val;
    }

    /**
    * @brief Safely translate multi-byte numbers coming off the USB wire into native CPU numbers.
    * @details The physical camera always sends its numbers in "Little-Endian" format. 
    * If you are running this on a standard Raspberry Pi, your computer already speaks 
    * this language, and this function completely disappears when compiled, costing zero 
    * performance. 
    * 
    * However, if you compile this on a system that reads numbers backward (Big-Endian), 
    * it will automatically flip the bytes so the hardware data isn't misinterpreted as 
    * corrupted garbage.
    * @param val The raw number directly from the USB wire.
    * @return The safely formatted native host number.
    */
    template <std::integral T>
    constexpr T wireToHost(T val) noexcept {
        if constexpr (std::endian::native == std::endian::big) {
            return std::byteswap(val);
        }
        return val;
    }

    /**
    * @brief Single-byte bypass for wire packaging.
    * @details A single byte has no endianness, so this safely returns the value exactly as-is.
    * @param val The native byte from our software.
    * @return The exact same byte.
    */
    constexpr uint8_t hostToWire(uint8_t val) noexcept {
        return val;
    }

    /**
    * @brief Safely package multi-byte native CPU numbers before sending them out over the USB wire.
    * @details Formats numbers into the strict byte order the camera hardware expects.
    * @param val The native host number from our software.
    * @return The number formatted for the USB wire.
    */
    template <std::integral T>
    constexpr T hostToWire(T val) noexcept {
        if constexpr (std::endian::native == std::endian::big) {
            return std::byteswap(val);
        }
        return val;
    }
}