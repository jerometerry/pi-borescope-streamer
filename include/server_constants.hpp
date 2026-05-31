#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdint>
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
     * @brief The button debounce time in milliseconds
     */
    inline constexpr int BUTTON_DEBOUNCE_TIME_MS = 200;

    /**
     *
     */
    inline constexpr int QUICK_PRESS_MIN_MS = 150;

    /**
     * 
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

    inline constexpr uint8_t GRAVITY_SENSOR_CAMERA_ID = 0x07;

    inline constexpr uint8_t VIDEO_CAMERA_ID = 0x0B;

    /**
     * @brief
     */
    inline constexpr uint8_t VALID_CAMERA_IDS[] = {GRAVITY_SENSOR_CAMERA_ID, VIDEO_CAMERA_ID};

    /**
     * @brief
     */
    inline constexpr uint16_t USB_FRAME_HEADER = 0xBBAA;

    /** 
     * @brief A list of vendor and product IDs for supported devices
     */
    inline constexpr std::pair<uint16_t, uint16_t> VENDOR_PRODUCT_ID_LIST[] = 
        {{0x2ce3, 0x3828}, {0x0329, 0x2022}};
}