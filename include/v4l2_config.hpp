#pragma once
#include <string>
#include <cstddef>
#include "server_constants.hpp"

/**
 * @brief Configuration parameters for the V4L2 loopback device.
 */
struct V4l2Config {
    std::string devicePath{"/dev/video7"};
    int width{640};
    int height{480};
    size_t sizeImage{ServerConstants::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES};
};