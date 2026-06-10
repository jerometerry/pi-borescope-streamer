/**
 * @file mjpeg_frame_extractor.cpp
 * @brief An offline tool to pull clean JPEG pictures out of raw camera dumps.
 * @details Single-threaded deterministic extraction to completely eliminate thread timing races.
 */

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <format>
#include <fstream>
#include <iostream>
#include <print>
#include <span>
#include <string>
#include <vector>
#include "constants.hpp"
#include "frame.hpp"
#include "frame_disruptor.hpp"
#include "mjpeg_stream.hpp"

int main(int argc, const char* argv[]) {
    std::cout << std::unitbuf;

    try {
        if (argc != 3) {
            std::println(std::cerr, "Usage: {} <path_to_bin_file> <target_frame_index>", argv[0]);
            return EXIT_FAILURE;
        }

        std::string inputPath{argv[1]};
        const int64_t targetFrameIndex = std::stoll(argv[2]);

        if (targetFrameIndex < 0) {
            std::println(std::cerr, "[Error] Target frame index must be 0 or greater.");
            return EXIT_FAILURE;
        }

        std::ifstream inFile(inputPath, std::ios::binary);
        if (!inFile) {
            std::println(std::cerr, "[Error] Failed to open input file: {}", inputPath);
            return EXIT_FAILURE;
        }

        // Initialize our production tracking components
        FrameDisruptor ringBuffer;
        for (int64_t i = 0; i < FRAME_DISRUPTOR_CAPACITY; ++i) {
            ringBuffer.getBySequence(i).preAllocate(Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);
        }

        MjpegStream streamDecoder(ringBuffer);

        int64_t next_inspect_sequence = 0;
        int64_t valid_image_counter = 0;
        bool frameFound = false;

        std::println(std::cout, "[Feeder] Commencing high-speed injection from dump file...");
        std::vector<uint8_t> virtualTransferBuffer(UsbConfig::BULK_TRANSFER_SIZE);

        while (inFile && !frameFound) {
            inFile.read(reinterpret_cast<char*>(virtualTransferBuffer.data()), UsbConfig::BULK_TRANSFER_SIZE);
            std::streamsize bytesRead = inFile.gcount();

            if (bytesRead > 0) {
                std::span<const uint8_t> payload(
                    virtualTransferBuffer.data(), 
                    static_cast<size_t>(bytesRead)
                );

                streamDecoder.send(payload); 

                int64_t available = ringBuffer.getHighestPublished();
                while (next_inspect_sequence <= available) {
                    Frame& slot = ringBuffer.getBySequence(next_inspect_sequence);

                    if (slot.active_size > 0) {
                        if (++valid_image_counter == targetFrameIndex) {
                            std::string outFilename = std::format("extracted_frame_{}.jpg", targetFrameIndex);
                            std::ofstream outImage(outFilename, std::ios::out | std::ios::binary);
                            outImage.write(reinterpret_cast<const char*>(slot.getContentSlice().data()), slot.active_size);
                            
                            std::println(std::cout, "[Success] Extracted valid frame #{} from pipeline slot {} ({} bytes)", 
                                         targetFrameIndex, next_inspect_sequence, slot.active_size);
                            
                            frameFound = true;
                            break;
                        }
                    }
                    
                    next_inspect_sequence++;
                }
            }
        }

        if (!frameFound) {
            std::println(std::cerr, "\n[Finished] File processed completely.");
            std::println(std::cerr, "  -> Total Pipeline Slots Parsed : {}", next_inspect_sequence);
            std::println(std::cerr, "  -> Total Valid JPEGs Discovered: {}", valid_image_counter); 
            std::println(std::cerr, "  -> Corrupt Frames Dropped (EMI): {}", next_inspect_sequence - valid_image_counter);
            std::println(std::cerr, "  -> Error                       : Target index {} is out of bounds for this file.", targetFrameIndex);
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;

    } catch (const std::exception& e) {
        std::println(std::cerr, "[Fatal Error] Terminated via unhandled exception: {}", e.what());
        return EXIT_FAILURE;
    }
}
