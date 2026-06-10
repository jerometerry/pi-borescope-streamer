/**
 * @file mjpeg_frame_extractor.cpp
 * @brief An offline tool to pull clean JPEG pictures out of raw camera dumps.
 * @details Corrected for extreme out-of-bounds target index deadlocks.
 */

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <span>
#include <format>
#include <print>
#include <thread>
#include <atomic>
#include "constants.hpp"
#include "frame.hpp"
#include "frame_disruptor.hpp"
#include "mjpeg_stream.hpp"

int main(int argc, const char* argv[]) {
    std::cout << std::unitbuf;

    try {
        if (argc != 3) {
            std::println(std::cerr, "Usage: {} <path_to_bin_file> <target_frame_index>", argv[0]);
            std::println(std::cerr, "Example: {} raw_camera_dump.bin 42", argv[0]);
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

        FrameDisruptor ringBuffer;
        ringBuffer.preAllocate(Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);

        MjpegStream streamDecoder(ringBuffer);

        std::atomic<bool> file_feeding_active{true};
        std::atomic<bool> frameFound{false};


        std::jthread extractorConsumer([&ringBuffer, &file_feeding_active, &frameFound, targetFrameIndex](const std::stop_token& st) {
            int64_t next_read = 0;

            while ((file_feeding_active.load(std::memory_order_acquire) || ringBuffer.getHighestPublished() >= next_read) && !st.stop_requested()) {
                int64_t available = ringBuffer.getHighestPublished();

                if (next_read <= available) {
                    while (next_read <= available) {
                        Frame& slot = ringBuffer.getBySequence(next_read);

                        if (next_read == targetFrameIndex) {
                            if (slot.active_size > 0) {
                                std::string outFilename = std::format("extracted_frame_{}.jpg", targetFrameIndex);
                                std::ofstream outImage(outFilename, std::ios::out | std::ios::binary);
                                outImage.write(reinterpret_cast<const char*>(slot.getContentSlice().data()), slot.active_size);
                                
                                std::println(std::cout, "[Success] Extracted target frame {} ({} bytes)", targetFrameIndex, slot.active_size);
                                frameFound.store(true, std::memory_order_release);
                            }
                            file_feeding_active.store(false, std::memory_order_release);
                            return;
                        }
                        next_read++;
                    }
                    ringBuffer.markConsumed(next_read - 1);
                } else {
                    #if defined(__x86_64__) || defined(_M_X64)
                        asm volatile("pause" ::: "memory");
                    #else
                        std::this_thread::yield();
                    #endif
                }
            }
        });

        std::println(std::cout, "[Feeder] Commencing high-speed injection from dump file...");
        std::vector<uint8_t> virtualTransferBuffer(UsbConfig::BULK_TRANSFER_SIZE);

        while (inFile && file_feeding_active.load(std::memory_order_relaxed)) {
            inFile.read(reinterpret_cast<char*>(virtualTransferBuffer.data()), UsbConfig::BULK_TRANSFER_SIZE);
            std::streamsize bytesRead = inFile.gcount();

            if (bytesRead > 0) {
                std::span<const uint8_t> payload(virtualTransferBuffer.data(), static_cast<size_t>(bytesRead));
                streamDecoder.send(payload); 
            }
        }

        file_feeding_active.store(false, std::memory_order_release);
        extractorConsumer.request_stop();
        extractorConsumer.join();

        if (!frameFound.load(std::memory_order_acquire)) {
            std::println(std::cerr, "[Finished] File processed completely. Highest valid stream index found was: {}.", ringBuffer.getHighestPublished());
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;

    } catch (const std::exception& e) {
        std::cerr << "[Fatal Error] Terminated via unhandled exception: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
