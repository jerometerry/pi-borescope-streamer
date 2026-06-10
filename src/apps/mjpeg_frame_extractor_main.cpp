/**
 * @file mjpeg_frame_extractor.cpp
 * @brief An offline tool to pull clean JPEG pictures out of raw camera dumps.
 * @details Single-threaded deterministic extraction to completely eliminate thread timing races.
 */

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <vector>
#include <format>
#include <print>
#include "buffer_pool.hpp"
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

        std::ifstream inFile(inputPath, std::ios::binary);
        if (!inFile) {
            std::println(std::cerr, "[Error] Failed to open input file: {}", inputPath);
            return EXIT_FAILURE;
        }

        auto pool = BufferPool::create();

        int64_t validFrames = 0;
        bool frameFound = false;

        auto onFrameReady = [&validFrames, targetFrameNumber, &frameFound](BufferPtr ptr) {
            if (ptr && ptr->contentSize() > 0) {
                if (++validFrames == targetFrameNumber) {
                    std::string filename = std::format("frame_{:04d}.jpg", validFrames);
                    std::ofstream image(filename, std::ios::out | std::ios::binary);

                    image.write(
                        reinterpret_cast<const char*>(ptr->getContentSlice().data()), 
                        ptr->contentSize()
                    );
                    
                    std::println(std::cout, "[Success] Extracted target frame #{} ({} bytes)", 
                                 targetFrameNumber, ptr->contentSize());
                    frameFound = true;
                }
            }
        };
        
        BufferedMjpegStream stream(pool, onFrameReady);

        std::println(std::cout, "[Feeder] Commencing high-speed injection from dump file...");
        std::vector<uint8_t> virtualTransferBuffer(UsbConfig::BULK_TRANSFER_SIZE);

        while (inFile && !frameFound) {
            inFile.read(
                reinterpret_cast<char*>(virtualTransferBuffer.data()), 
                UsbConfig::BULK_TRANSFER_SIZE
            );
            std::streamsize bytesRead = inFile.gcount();

            if (bytesRead > 0) {
                std::span<const uint8_t> payload(
                    virtualTransferBuffer.data(), 
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
