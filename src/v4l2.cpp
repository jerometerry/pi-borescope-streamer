#include <string>
#include <cstddef>
#include <iostream>
#include "argument_parser.hpp"
#include "v4l2.hpp"

namespace V4L2 {
    Arguments::ParseResult parseArguments(int argc, const char* argv[], V4L2::Config& config) {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            
            try {
                if (arg == "--dev" && i + 1 < argc) {
                    config.devicePath = argv[++i];
                } else if (arg == "--bus" && i + 1 < argc) {
                    unsigned long val = std::stoul(argv[++i]);
                    if (val > 255) {
                        throw std::out_of_range("bus exceeds uint8_t max range");
                    }
                    config.bus = static_cast<uint8_t>(val);
                } else if (arg == "--address" && i + 1 < argc) {
                    unsigned long val = std::stoul(argv[++i]);
                    if (val > 255) {
                        throw std::out_of_range("address exceeds uint8_t max range");
                    }
                    config.address = static_cast<uint8_t>(val);
                } else if (arg == "--width" && i + 1 < argc) {
                    config.width = std::stoi(argv[++i]);
                } else if (arg == "--height" && i + 1 < argc) {
                    config.height = std::stoi(argv[++i]);
                } else if (arg == "--size" && i + 1 < argc) {
                    config.sizeImage = std::stoull(argv[++i]);
                } else if (arg == "--help") {
                    std::cout << "Usage: v4l2-borescope-daemon [options]\n"
                            << "Options:\n"
                            << "  --dev <path>     Path to loopback device (default: /dev/video7)\n"
                            << "  --bus <byte>     Bus ID of USB camera (default: 0)\n"
                            << "  --address <byte> Bus Address of USB Camera (default: 0)\n"
                            << "  --width <px>     Video width (default: 640)\n"
                            << "  --height <px>    Video height (default: 480)\n"
                            << "  --size <bytes>   Max frame buffer size (default: 131072)\n"
                            << "  --help           Show this message\n";
                    return Arguments::ParseResult::HelpRequested;
                } else {
                    std::cerr << "Unknown argument: " << arg << "\n";
                    return Arguments::ParseResult::Error;
                }
            } catch (const std::exception& e) {
                std::cerr << "Invalid value provided for argument " << arg << "\n";
                return Arguments::ParseResult::Error;
            }
        }
        
        return Arguments::ParseResult::Success;
    }

}
