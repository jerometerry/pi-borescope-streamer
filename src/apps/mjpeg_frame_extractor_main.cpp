/**
 * @file frame_extractor.cpp
 * @brief An offline tool to pull clean JPEG pictures out of raw camera dumps.
 * @details This is the companion tool to `binary_stream_capture`. If you record a raw 
 * `.bin` or `.mjpeg` file from the camera, you can feed it into this tool. It will 
 * scan the raw byte data, find the hidden camera headers, strip them away, stitch the 
 * video chunks back together, and save perfect `.jpg` image files to your hard drive. 
 * 
 * It is essentially the `MjpegStream` class pulled out of the live server and 
 * turned into a standalone command-line tool.
 */

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "mjpeg_frame_extractor.hpp"

int main(int argc, const char* argv[]) {
    try {
        if (argc != 3) {
            std::cerr << "Usage: " << argv[0] << " <path_to_bin_file> <frame_index>\n";
            std::cerr << "Example: " << argv[0] << " raw_camera_dump.bin 2\n";
            return EXIT_FAILURE;
        }

        std::string inputPath{argv[1]};
        int targetFrameIndex = std::stoi(argv[2]);

        if (targetFrameIndex < 1) {
            std::cerr << "[Error] Target frame index must be 1 or greater.\n";
            return EXIT_FAILURE;
        }

        std::ifstream inFile(inputPath.data(), std::ios::binary | std::ios::ate);
        if (!inFile) {
            std::cerr << "[Error] Failed to open: " << inputPath << "\n";
            return EXIT_FAILURE;
        }

        std::streamsize size = inFile.tellg();
        inFile.seekg(0, std::ios::beg);
        
        std::vector<uint8_t> fileData(size);
        if (!inFile.read(reinterpret_cast<char*>(fileData.data()), size)) {
            std::cerr << "[Error] Failed to read data from file.\n";
            return EXIT_FAILURE;
        }

        std::cout << "File size: " << fileData.size() << '\n';

        MjpegFrameExtractor::extractFrames(fileData);

        return EXIT_SUCCESS;

    } catch (const std::exception& e) {
        std::cerr << "[Fatal Error] Unhandled exception in main: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
