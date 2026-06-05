#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

/**
 * @brief The master dial board and blueprint for the entire system.
 * @details When building hardware projects, burying "magic numbers" (like buffer 
 * sizes, HTTP strings, or specific camera IDs) deep inside the code makes it 
 * incredibly difficult to update or modify the system later. 
 * 
 * This namespace acts as the single source of truth for every hardcoded value 
 * in the server. If you want to increase the maximum number of viewers, change 
 * the memory bucket sizes, or add support for a brand new endoscope model's 
 * Vendor ID, you only ever have to change it right here.
 */
namespace ServerConstants {

    /**
     * @brief The baseline byte count for a standard memory kilobyte.
     */
    inline constexpr size_t ONE_KILOBYTE = 1024;

    /**
     * @brief Standard memory bucket size for small network reads (e.g., incoming HTTP text).
     */
    inline constexpr size_t FOUR_KILOBYTES = 4 * ONE_KILOBYTE;

    /**
     * @brief Standard memory bucket size for intermediate processing buffers.
     */
    inline constexpr size_t EIGHT_KILOBYTES = 8 * ONE_KILOBYTE;

    /**
     * @brief The absolute maximum byte size expected for a single incoming JPEG picture.
     */
    inline constexpr size_t FORTY_KILOBYTES = 40 * ONE_KILOBYTE;

    /**
     * @brief Standard memory bucket size for moderate stream processing.
     */
    inline constexpr size_t SIXTY_FOUR_KILOBYTES = 64 * ONE_KILOBYTE;

    /**
     * @brief The massive memory bucket size used for the main shared video canvases.
     */
    inline constexpr size_t ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES = 128 * ONE_KILOBYTE;

    /**
     * @brief Safe-zone memory allocation size for extracting unchunked raw frames.
     */
    inline constexpr size_t TWO_HUNDRED_FIFTY_SIX_KILOBYTES = 256 * ONE_KILOBYTE;

    /**
     * @brief The baseline byte count for a standard memory megabyte.
     */
    inline constexpr size_t ONE_MEGABYTE   = ONE_KILOBYTE * ONE_KILOBYTE;

    /**
     * @brief Standard memory size for large disk-write operations.
     */
    inline constexpr size_t TWO_MEGABYTES  = 2 * ONE_MEGABYTE;

    /**
     * @brief How long (in ms) we will wait for the USB hardware to respond before assuming it disconnected.
     */
    inline constexpr unsigned int USB_TIMEOUT = 1000;

    /**
     * @brief 
     */
    inline constexpr int INITIAL_SHARED_FRAME_POOL_SIZE = 4;

    /**
     * @brief 
     */
    inline constexpr int MAX_SHARED_FRAME_POOL_SIZE = 8;

    /**
     * @brief 
     */
    inline constexpr int USB_TRANSFER_BUFFER_POOL_SIZE = 8;
    
    /**
     * @brief 
     */
    inline constexpr int CHUNK_SIZE = ServerConstants::FOUR_KILOBYTES;

    /**
     * @brief 
     */
    inline constexpr int TEN_MILLISECONDS = 10000;

    /**
     * @brief 
     */
    inline constexpr int ONE_HUNDRED_MILLISECONDS = 100000;

    /**
     * @brief The absolute maximum number of web browsers allowed to watch the live feed simultaneously.
     */
    inline constexpr size_t MAX_CLIENTS = 16;

    /**
     * @brief The size of the fast temporary memory stack used to build network text headers.
     */
    inline constexpr size_t STACK_BUF_SIZE = 128;

    /**
     * @brief The electrical "noise filter". Ignores rapidly stuttering button signals under this duration.
     */
    inline constexpr int BUTTON_DEBOUNCE_TIME_MS = 200;

    /**
     * @brief The minimum amount of time a button must be held down to be considered a deliberate press.
     */
    inline constexpr int QUICK_PRESS_MIN_MS = 150;

    /**
     * @brief If the button is held longer than this, it is considered a "long press" rather than a quick snapshot click.
     */
    inline constexpr int QUICK_PRESS_MAX_MS = 450;

    /**
     * @brief The universal mathematical signature (Start of Image) that begins every valid JPEG file.
     */
    inline constexpr uint8_t JPEG_SOI_MARKERS[] = { 0xFF, 0xD8 };

    /**
     * @brief We will stop searching for the JPEG start signature if we don't find it within the first 32 bytes of a payload.
     */
    inline constexpr int JPEG_SOI_MARKERS_MAX_POSITION = 32;

    /**
     * @brief The internal hardware lens ID for endoscopes equipped with a physical orientation sensor.
     */
    inline constexpr uint8_t GRAVITY_SENSOR_CAMERA_ID = 0x07;

    /**
     * @brief The internal hardware lens ID for standard, non-gravity video endoscopes.
     */
    inline constexpr uint8_t VIDEO_CAMERA_ID = 0x0B;

    /**
     * @brief A registry of all known internal camera lenses this software knows how to decode.
     */
    inline constexpr uint8_t VALID_CAMERA_IDS[] = {GRAVITY_SENSOR_CAMERA_ID, VIDEO_CAMERA_ID};

    /**
     * @brief The secret 0xBBAA "shipping label" code the hardware uses to tag a valid video chunk on the wire.
     */
    inline constexpr uint16_t USB_FRAME_HEADER = 0xBBAA;

    /**
     * @brief The official hardware whitelist. The software will only connect to cameras matching these Manufacturer and Model IDs.
     */
    inline constexpr std::pair<uint16_t, uint16_t> VENDOR_PRODUCT_ID_LIST[] = {
        {0x2ce3, 0x3828}, 
        {0x0329, 0x2022}
    };
}