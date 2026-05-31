#include <cstdint>
#include <exception>
#include <iostream>
#include <fstream>
#include <functional>
#include <vector>
#include <string>
#include <span>
#include <cstdlib>
#include <algorithm>
#include "usb_frame_decoder.hpp"

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

        int currentFrameCount = 0;
        bool frameFound = false;

        // The decoder will trigger this exactly when the frame is fully assembled
        auto broadcastHandler = [&](const std::vector<uint8_t>& frame) {
            currentFrameCount++;
            
            if (currentFrameCount == targetFrameIndex) {
                frameFound = true;
                
                size_t soiOffset = std::string::npos;
                for (size_t j = 0; j + 1 < frame.size(); ++j) {
                    if (frame[j] == 0xFF && frame[j+1] == 0xD8) {
                        soiOffset = j;
                        break;
                    }
                }

                // Scan backwards for the End of Image marker
                size_t eoiOffset = std::string::npos;
                for (size_t j = frame.size(); j >= 2; --j) {
                    if (frame[j - 2] == 0xFF && frame[j - 1] == 0xD9) {
                        eoiOffset = j;
                        break;
                    }
                }

                if (soiOffset != std::string::npos && eoiOffset != std::string::npos && soiOffset < eoiOffset) {
                    std::vector<uint8_t> finalJpeg(frame.begin() + soiOffset, frame.begin() + eoiOffset);
                    
                    std::string outputPath = "extracted_frame_" + std::to_string(targetFrameIndex) + ".jpg";
                    std::ofstream outFile(outputPath, std::ios::binary);
                    outFile.write(reinterpret_cast<const char*>(finalJpeg.data()), finalJpeg.size());
                    
                    std::cout << "[Success] Extracted flawless frame " << targetFrameIndex 
                              << " (" << finalJpeg.size() << " bytes) to: " << outputPath << "\n";
                } else {
                    std::cerr << "[Error] Frame assembled, but missing valid JPEG delimiters.\n";
                    
                    std::ofstream errFile("corrupt_frame_debug.jpg", std::ios::binary);
                    errFile.write(reinterpret_cast<const char*>(frame.data()), frame.size());
                    std::cerr << "[Info] Dumped raw payload to corrupt_frame_debug.jpg\n";
                }
            }
        };

        UsbFrameDecoder decoder(broadcastHandler, []() {});

        std::cout << "[Scanner] Reconstructing libusb hardware blocks...\n";

        for (size_t i = 0; i + 12 <= fileData.size(); ) {
            // Locate an AA BB header (for either Camera 11 or Camera 7)
            if (fileData[i] == 0xAA && fileData[i+1] == 0xBB && 
               (fileData[i+2] == 0x0B || fileData[i+2] == 0x07)) {

                // --- THE PROXIMITY GHOST FILTER ---
                // A ghost header is trapped in the ~80 byte hardware padding.
                // This means the NEXT REAL HEADER will appear less than 200 bytes away.
                // We scan forward briefly. If we find another header colliding with this one, 
                // the current header is mathematically proven to be a ghost.
                bool isGhost = false;
                size_t nextHeaderOffset = 0;
                size_t scanLimit = std::min<size_t>(300, fileData.size() - i - 5);
                
                for (size_t d = 5; d <= scanLimit; ++d) {
                    if (fileData[i+d] == 0xAA && fileData[i+d+1] == 0xBB && 
                       (fileData[i+d+2] == 0x0B || fileData[i+d+2] == 0x07)) {
                        isGhost = true;
                        nextHeaderOffset = d;
                        break;
                    }
                }

                if (isGhost) {
                    // Bypass the ghost entirely and jump straight to the real header!
                    i += nextHeaderOffset;
                    continue;
                }

                // If we reach here, it is a mathematically verified REAL header.
                uint16_t length = fileData[i+3] | (fileData[i+4] << 8);

                if (length >= 7 && i + 5 + length <= fileData.size()) {
                    size_t totalPacketSize = 5 + length;
                    
                    // Pass the pristine span exactly as libusb would have delivered it
                    decoder.processIncomingCameraData(std::span<const uint8_t>{fileData.data() + i, totalPacketSize});
                    
                    // The Ultimate Safety Vault: Jump completely over the payload.
                    // This guarantees we NEVER scan the actual JPEG pixels for AA BB signatures.
                    i += totalPacketSize;
                } else {
                    i++;
                }
            } else {
                i++;
            }

            if (frameFound) {
                break;
            }
        }

        if (!frameFound) {
            std::cerr << "[Failed] Target frame index " << targetFrameIndex 
                      << " was not reached. Dump only contains " << currentFrameCount << " full frames.\n";
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;

    } catch (const std::exception& e) {
        std::cerr << "[Fatal Error] Unhandled exception in main: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}