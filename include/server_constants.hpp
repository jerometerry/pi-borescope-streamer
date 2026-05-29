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
}