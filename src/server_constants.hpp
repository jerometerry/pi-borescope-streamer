#pragma once

#include <cstddef>

namespace ServerConstants {
    inline constexpr size_t ONE_KILOBYTE = 1024;
    inline constexpr size_t FOUR_KILOBYTES = 4 * ONE_KILOBYTE;
    inline constexpr size_t ONE_MEGABYTE   = ONE_KILOBYTE * ONE_KILOBYTE;
    inline constexpr size_t TWO_MEGABYTES  = 2 * ONE_MEGABYTE;
}