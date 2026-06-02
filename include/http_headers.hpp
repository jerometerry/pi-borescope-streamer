#pragma once

#include <cstddef>
#include <string_view>

namespace HttpHeaders {
	/**
     * @brief The HTTP label that separates one picture from the next.
     * @details Web browsers need this exact text boundary to know when one JPEG ends 
     * and the next one begins, which creates the illusion of smooth video.
     */
    inline constexpr std::string_view MJPEG_CHUNK_PREFIX = 
        "--mjpegstream\r\nContent-Type: image/jpeg\r\nContent-Length: ";
    
    /** 
     * @brief The suffix for MJPEG chunks
     */
    inline constexpr std::string_view MJPEG_CHUNK_SUFFIX = "\r\n\r\n";
}