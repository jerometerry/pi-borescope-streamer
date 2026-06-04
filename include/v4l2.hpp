#pragma once
#include <string>
#include <cstddef>
#include "argument_parser.hpp"
#include "server_constants.hpp"

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
        size_t sizeImage{ServerConstants::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES};
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
