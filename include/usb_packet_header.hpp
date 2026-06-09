#pragma once
#include <atomic>
#include <bit>
#include <concepts>
#include <cstdint>
#include <vector>
#include "endian_conversion.hpp"

/**
* @brief The outer "shipping envelope" that safely transports data across the USB cable.
* @details In network terms, this is the transport layer. When the camera fires data down 
* the wire, it places every chunk of video into this exact 5-byte envelope. We read this 
* outer header first to know exactly how many bytes are inside, ensuring we never read 
* out of bounds and crash the server. 
* 
* Once we verify this envelope is valid and safe to open, we strip it away to reveal 
* the actual inner payload (which begins with the PayloadHeader).
*/
struct [[gnu::packed]] UsbPacketHeader {

    /**
    * @brief The raw, un-translated secret code identifying this as a valid camera chunk.
    */
    uint16_t leHeader;

    /**
    * @brief Which physical camera lens this data came from (used if the endoscope has multiple lenses).
    */
    uint8_t leCameraId;

    /**
    * @brief The raw, un-translated size of the video payload inside this envelope.
    */
    uint16_t leLength;

    /**
    * @brief Read the header verification code safely.
    * @return The translated verification code, ready for our software to check.
    */
    constexpr uint16_t getHeader() const noexcept { 
        return EndianConversion::wireToHost(leHeader); 
    }

    /**
    * @brief Write the header verification code safely.
    * @param val The code to package for the camera.
    */
    constexpr void setHeader(uint16_t val) noexcept { 
        leHeader = EndianConversion::hostToWire(val); 
    }

    /**
    * @brief Get the ID of the camera lens that generated this chunk.
    * @return The safe, translated camera ID.
    */
    constexpr uint8_t getCameraId() const noexcept { 
        return EndianConversion::wireToHost(leCameraId); 
    }

    /**
    * @brief Set the ID of the camera lens generating this chunk.
    * @param val The camera ID.
    */
    constexpr void setCameraId(uint8_t val) noexcept { 
        leCameraId = EndianConversion::hostToWire(val); 
    }

    /**
    * @brief Check exactly how many bytes of video data are enclosed in this envelope.
    * @return The safe, translated length of the inner payload.
    */
    constexpr uint16_t getLength() const noexcept { 
        return EndianConversion::wireToHost(leLength); 
    }

    /**
    * @brief Set the length of the data chunk before sending it.
    * @param val The length in bytes.
    */
    constexpr void setLength(uint16_t val) noexcept { 
        leLength = EndianConversion::hostToWire(val); 
    }
};
