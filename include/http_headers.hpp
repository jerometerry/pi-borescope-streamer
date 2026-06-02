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

    /** 
     * @brief The delimiter for HTTP headers
     */
    inline constexpr std::string_view HEADER_DELIMITER = "\r\n\r\n";

    /** 
     * @brief The format string for MJPEG frame headers
     */
    inline constexpr std::string_view MJPEG_FRAME_FMT = 
        "--mjpegstream\r\nContent-Type: image/jpeg\r\nContent-Length: {}\r\n\r\n";

    /** 
     * @brief The format string for MJPEG frame footers
     */
    inline constexpr std::string_view MJPEG_FOOTER = "\r\n";

    /**
     * @brief Standard web browser greeting used to deliver text-based dashboards.
     */
    inline constexpr std::string_view HTTP_OK_HTML_HDR =
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: ";

    /**
     * @brief Standard web browser greeting used to deliver a single, static picture.
     */
    inline constexpr std::string_view HTTP_OK_JPEG_HDR =
        "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nCache-Control: no-store, max-age=0\r\nContent-Length: ";

    /**
     * @brief The mandatory blank line required to tell a web browser that our greeting is finished.
     */
    inline constexpr std::string_view HTTP_HDR_END =
        "\r\nConnection: close\r\n\r\n";

    /**
     * @brief The complex web browser greeting required to establish a continuous, infinite video stream.
     */
    inline constexpr std::string_view HTTP_OK_MJPEG =
        "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: no-cache, private\r\nPragma: no-cache\r\nContent-Type: multipart/x-mixed-replace; boundary=mjpegstream\r\n\r\n";

    /**
    * @brief Standard web browser greeting used to deliver structured JSON data.
    */
    inline constexpr std::string_view HTTP_OK_JSON_HDR =
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ";

    /**
     * @brief Standard web browser rejection used when a viewer asks for a page that doesn't exist.
     */
    inline constexpr std::string_view HTTP_NOT_FOUND = 
        "HTTP/1.1 404 Not Found\r\nCache-Control: no-cache\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";

    /**
     * @brief Silently ignores browser requests for website icons to save bandwidth.
     */
    inline static constexpr std::string_view FAVICON_NOT_FOUND = 
        "HTTP/1.1 404 Not Found\r\nCache-Control: public, max-age=31536000\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
}