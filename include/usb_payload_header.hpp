#pragma once

#include <atomic>
#include <bit>
#include <concepts>
#include <cstdint>
#include <vector>
#include "endian_conversion.hpp"

/**
* @brief The internal assembly instructions for the actual camera payload.
* @details When you strip away the outer USB packet, you are left with the raw camera payload. 
* Because a single JPEG picture is too large to fit in one transfer, the camera chops it up. 
* This 7-byte header sits at the front of the inner payload, providing the sequence number 
* (`frameId`) needed to stitch the picture back together in the correct order.
* 
* Rather than creating a separate data channel for the physical hardware sensors, the camera's 
* engineers cleverly used the remaining bytes in this header to piggyback the gravity sensor and 
* button state alongside the video data.
*/
struct [[gnu::packed]] UsbPayloadHeader {

    /**
    * @brief A rolling counter that helps us stitch chunks together into a full picture.
    */
    uint8_t leFrameId;

    /**
    * @brief Identifies which lens is active on dual-lens endoscopes.
    */
    uint8_t leCameraNumber;

    /**
    * @brief A densely packed byte where each bit represents a yes/no switch (like a button press).
    */
    uint8_t leFlags;

    /**
    * @brief The raw, un-translated orientation data piggybacked from the camera's gyroscope.
    */
    uint32_t leGravitySensor;

    /**
    * @brief Get the sequence ID of the frame this chunk belongs to.
    * @return The safe, translated frame ID.
    */
    constexpr uint8_t getFrameId() const noexcept { 
        return EndianConversion::wireToHost(leFrameId); 
    }

    /**
    * @brief Set the sequence ID for the frame this chunk belongs to.
    * @param val The frame ID.
    */
    constexpr void setFrameId(uint8_t val) noexcept { 
        leFrameId = EndianConversion::hostToWire(val); 
    }

    /**
    * @brief Get the internal camera lens number.
    * @return The safe, translated camera number.
    */
    constexpr uint8_t getCameraNumber() const noexcept { 
        return EndianConversion::wireToHost(leCameraNumber); 
    }

    /**
    * @brief Set the internal camera lens number.
    * @param val The camera number.
    */
    constexpr void setCameraNumber(uint8_t val) noexcept { 
        leCameraNumber = EndianConversion::hostToWire(val); 
    }

    /**
    * @brief Get the raw hardware flags byte.
    * @return The safe, translated flags byte.
    */
    constexpr uint8_t getFlags() const noexcept { 
        return EndianConversion::wireToHost(leFlags); 
    }

    /**
    * @brief Set the raw hardware flags byte.
    * @param val The flags byte to pack.
    */
    constexpr void setFlags(uint8_t val) noexcept { 
        leFlags = EndianConversion::hostToWire(val); 
    }

    /**
    * @brief Get the camera's physical orientation safely.
    * @return The translated gyroscope reading.
    */
    constexpr uint32_t getGravitySensor() const noexcept { 
        return EndianConversion::wireToHost(leGravitySensor); 
    }

    /**
    * @brief Set the camera's physical orientation safely.
    * @param val The gyroscope reading to pack.
    */
    constexpr void setGravitySensor(uint32_t val) noexcept { 
        leGravitySensor = EndianConversion::hostToWire(val); 
    }

    /**
    * @brief Check if the camera handle actually has a gravity sensor installed.
    * @return True if the hardware supports orientation tracking.
    */
    constexpr bool hasGravitySensor() const noexcept { 
        return (getFlags() & 0x01) != 0; 
    }

    /**
    * @brief Toggle the flag indicating if the hardware has a gravity sensor.
    * @param hasGravitySensor True to turn the flag on, false to turn it off.
    */
    constexpr void setHasGravitySensor(bool hasGravitySensor) noexcept { 
        uint8_t current = getFlags();
        if (hasGravitySensor) { 
            current |= 0x01; 
        } else { 
            current &= ~0x01; 
        }
        setFlags(current);
    }

    /**
    * @brief Check if the user is actively pressing the physical button on the camera handle.
    * @return True if the button is currently held down.
    */
    constexpr bool isButtonPressed() const noexcept { 
        return (getFlags() & 0x02) != 0; 
    }

    /**
    * @brief Simulate or set the state of the physical camera button.
    * @param pressed True to mark the button as pressed.
    */
    constexpr void setButtonPressed(bool pressed) noexcept {
        uint8_t current = getFlags();
        if (pressed) {
            current |= 0x02;
        } else {
            current &= ~0x02;
        }
        setFlags(current);
    }

    /**
    * @brief Extract any extra unknown or reserved flags from the camera.
    * @return A clean byte containing only the reserved hardware flags.
    */
    constexpr uint8_t getOtherFlags() const noexcept { 
        return (getFlags() >> 2) & 0x3F; 
    }

    /**
    * @brief Set the extra unknown or reserved hardware flags.
    * @param val The flags to pack into the remaining bits.
    */
    constexpr void setOtherFlags(uint8_t val) noexcept {
        uint8_t current = getFlags();
        current &= 0x03;
        current |= ((val & 0x3F) << 2); 
        setFlags(current);
    }
};


static_assert(
    sizeof(UsbPayloadHeader) == 7, 
    "PayloadHeader size must be exactly 7 bytes to match the hardware protocol!"
);
