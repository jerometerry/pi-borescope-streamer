#pragma once
#include <stdint.h>
#include <string>
#include <cstddef>
#include "constants.hpp"

namespace V4L2 {
    /**
    * @brief Configuration parameters for the V4L2 loopback device.
    */
    struct Config {
        std::string devicePath{"/dev/video7"};
        uint8_t bus{0};
        uint8_t address{0};
        int width{640};
        int height{480};
        size_t sizeImage{Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES};

        /**
         * @brief Reset config values back to defaults
         */
        void reset() {
            devicePath = "/dev/video7";
            bus = 0;
            address = 0;
            width = 640;
            height = 480;
            sizeImage = Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES;
        }
    };

    /**
     * @brief 
     * 
     * @param argc 
     * @param argv 
     * @param config 
     * @return Arguments::ParseResult 
     */
    Arguments::ParseResult parseArguments(int argc, const char* argv[], V4L2::Config& config);
}
