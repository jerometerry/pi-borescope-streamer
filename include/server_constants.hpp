#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

/** 
 * @brief Namespace for server constants
 */
namespace ServerConstants {

    /** 
     * @brief The size of one kilobyte
     */
    inline constexpr size_t ONE_KILOBYTE = 1024;

    /** 
     * @brief The size of four kilobytes
     */
    inline constexpr size_t FOUR_KILOBYTES = 4 * ONE_KILOBYTE;

    /** 
     * @brief The size of 8 kilobytes
     */
    inline constexpr size_t EIGHT_KILOBYTES = 8 * ONE_KILOBYTE;

    /** 
     * @brief The size of 40 kilobytes
     */
    inline constexpr size_t FORTY_KILOBYTES = 40 * ONE_KILOBYTE;

    /** 
     * @brief The size of 64 kilobytes
     */
    inline constexpr size_t SIXTY_FOUR_KILOBYTES = 64 * ONE_KILOBYTE;

    /** 
     * @brief The size of 128 kilobytes
     */
    inline constexpr size_t ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES = 128 * ONE_KILOBYTE;

    /** 
     * @brief The size of 256 kilobytes
     */
    inline constexpr size_t TWO_HUNDRED_FIFTY_SIX_KILOBYTES = 256 * ONE_KILOBYTE;

    /** 
     * @brief The size of one megabyte
     */
    inline constexpr size_t ONE_MEGABYTE   = ONE_KILOBYTE * ONE_KILOBYTE;

    /** 
     * @brief The size of two megabytes
     */
    inline constexpr size_t TWO_MEGABYTES  = 2 * ONE_MEGABYTE;

    /** 
     * @brief The USB timeout in milliseconds
     */
    inline constexpr unsigned int USB_TIMEOUT = 1000;

    /** 
     * @brief The maximum number of concurrent clients
     */
    inline constexpr size_t MAX_CLIENTS = 16;

    /** 
     * @brief The size of the stack buffer
     */
    inline constexpr size_t STACK_BUF_SIZE = 128;

    /** 
     * @brief The HTTP OK HTML header
     */
    inline constexpr std::string_view HTTP_OK_HTML_HDR =
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: ";

    /** 
     * @brief The HTTP OK JPEG header
     */
    inline constexpr std::string_view HTTP_OK_JPEG_HDR =
        "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: ";

    /** 
     * @brief The HTTP header end
     */
    inline constexpr std::string_view HTTP_HDR_END =
        "\r\nConnection: close\r\n\r\n";

    /** 
     * @brief The HTTP OK MJPEG header
     */
    inline constexpr std::string_view HTTP_OK_MJPEG =
        "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: no-cache, private\r\nPragma: no-cache\r\nContent-Type: multipart/x-mixed-replace; boundary=mjpegstream\r\n\r\n";

    /** 
     * @brief The HTTP NOT FOUND header
     */
    inline constexpr std::string_view HTTP_NOT_FOUND = 
        "HTTP/1.1 404 Not Found\r\nCache-Control: no-cache\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";

    /** 
     * @brief The FAVICON NOT FOUND header
     */
    inline static constexpr std::string_view FAVICON_NOT_FOUND = 
        "HTTP/1.1 404 Not Found\r\nCache-Control: public, max-age=31536000\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";

    /** 
     * @brief The button debounce time in milliseconds
     */
    inline constexpr int BUTTON_DEBOUNCE_TIME_MS = 200;

    /** 
     * @brief The minimum time for a quick press in milliseconds
     */
    inline constexpr int QUICK_PRESS_MIN_MS = 150;

    /**
     * @brief The maximum time for a quick press in milliseconds
     */
    inline constexpr int QUICK_PRESS_MAX_MS = 450;

    /** 
     * @brief The JPEG SOI markers
     */
    inline constexpr uint8_t JPEG_SOI_MARKERS[] = { 0xFF, 0xD8 };

    /** 
     * @brief The maximum position of the JPEG SOI markers
     */
    inline constexpr int JPEG_SOI_MARKERS_MAX_POSITION = 32;

    /** 
     * @brief The camera ID for the gravity sensor camera
     */
    inline constexpr uint8_t GRAVITY_SENSOR_CAMERA_ID = 0x07;

    /** 
     * @brief The camera ID for the video camera
     */
    inline constexpr uint8_t VIDEO_CAMERA_ID = 0x0B;

    /** 
     * @brief An array of valid camera IDs
     */
    inline constexpr uint8_t VALID_CAMERA_IDS[] = {GRAVITY_SENSOR_CAMERA_ID, VIDEO_CAMERA_ID};

    /** 
     * @brief The USB frame header
     */
    inline constexpr uint16_t USB_FRAME_HEADER = 0xBBAA;

    /** 
     * @brief A list of vendor and product IDs for supported devices
     */
    inline constexpr std::pair<uint16_t, uint16_t> VENDOR_PRODUCT_ID_LIST[] = {
        {0x2ce3, 0x3828}, 
        {0x0329, 0x2022}
    };
}