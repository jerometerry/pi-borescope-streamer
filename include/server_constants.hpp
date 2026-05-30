#pragma once

#include <cstddef>

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
     * @brief The JPEG SOI markers
     */
    inline constexpr uint8_t JPEG_SOI_MARKERS[] = { 0xFF, 0xD8 };

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
     * @brief The maximum position of the JPEG SOI markers
     */
    inline constexpr int JPEG_SOI_MARKERS_MAX_POSITION = 32;
}