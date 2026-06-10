/**
 * @file mjpeg_frame_extractor.cpp
 * @brief An offline tool to pull clean JPEG pictures out of raw camera dumps.
 * @details Single-threaded deterministic extraction to completely eliminate thread timing races.
 */

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include "buffer.hpp"
#include "buffer_pool.hpp"
#include "buffer_ptr.hpp"
#include "buffered_mjpeg_stream.hpp"
#include "constants.hpp"

int main(int argc, const char* argv[]) {
    std::cout << std::unitbuf;

    try {
        if (argc != 3) {
            std::println(std::cerr, "Usage: {} <path_to_bin_file> <target_frame_number>", argv[0]);
            std::println(std::cerr, "Example: {} dump.mjpeg 1 (Extracts the very first valid frame)", argv[0]);
            return EXIT_FAILURE;
        }

        std::string inputPath{argv[1]};
        const int64_t targetFrameNumber = std::stoll(argv[2]);

        if (targetFrameNumber < 1) {
            std::println(std::cerr, "[Error] Target frame number must be 1 or greater.");
            return EXIT_FAILURE;
        }

        auto pool = BufferPool::create();

        int64_t totalFrames = 0;
        int64_t validFrames = 0;
        bool frameFound = false;

        auto onFrameReady = [
            &validFrames, 
            &totalFrames,
            targetFrameNumber, 
            &frameFound
        ](const BufferPtr& ptr) {
            totalFrames++;
            std::cout << "Current Frame: " << totalFrames << "\n";

            if (totalFrames == targetFrameNumber) {
                std::cout << "Target Frame Found " << targetFrameNumber << "\n";

                std::string filename = std::format("frame_{:04d}.jpg", validFrames);
                std::ofstream image(filename, std::ios::out | std::ios::binary);

                image.write(
                    reinterpret_cast<const char*>(ptr->getContentSlice().data()), 
                    ptr->contentSize()
                );
                
                std::cout << "[Success] Extracted target frame # " << totalFrames 
                          << " bytes " << ptr->contentSize() << "\n";
                                
                frameFound = true;
            } else {
                std::cout << "Target Frame Not Found " << targetFrameNumber << "\n";
            }
        };
        
        BufferedMjpegStream stream(pool, onFrameReady);
        std::ifstream file(inputPath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Error opening file!\n";
            return EXIT_FAILURE;
        }

        std::vector<uint8_t> buffer(UsbConfig::BULK_TRANSFER_SIZE);
        char* bufferPtr = reinterpret_cast<char*>(buffer.data());

        while (file.read(bufferPtr, UsbConfig::BULK_TRANSFER_SIZE) || file.gcount() > 0) {
            std::streamsize bytesRead = file.gcount();
            std::cout << "Read page of " << bytesRead << " bytes.\n";
            
            if (bytesRead > 0) {
                std::span<const uint8_t> payload(
                    buffer.data(), 
                    static_cast<size_t>(bytesRead)
                );

                stream.send(payload);
            }            
        }

        if (frameFound) {
            std::println(std::cout, "\nExtraction complete. Successfully located target.");
        } else {
            std::println(std::cerr, "\n[Finished] File processed completely.");
            std::println(std::cerr, "  -> Total Valid JPEGs Discovered: {}", validFrames); 
            std::println(std::cerr, "  -> Error                       : Target frame #{} is out of bounds for this file.", targetFrameNumber);
            return EXIT_FAILURE;
        }

    } catch (const std::exception& e) {
        std::cerr << "[Fatal Error] Terminated via unhandled exception: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
