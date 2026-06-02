/**
 * @file frame_extractor.cpp
 * @brief An offline tool to pull clean JPEG pictures out of raw camera dumps.
 * @details This is the companion tool to `binary_stream_capture`. If you record a raw 
 * `.bin` or `.mjpeg` file from the camera, you can feed it into this tool. It will 
 * scan the raw byte data, find the hidden camera headers, strip them away, stitch the 
 * video chunks back together, and save perfect `.jpg` image files to your hard drive. 
 * 
 * It is essentially the `MjpegFrameDecoder` class pulled out of the live server and 
 * turned into a standalone command-line tool.
 */

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <format>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "data_structures.hpp"
#include "server_constants.hpp"

struct DumpRange {
    size_t start;
    size_t end;
};

struct Padding {
    size_t start;
    size_t length;
};

void printPaddingDump(const std::vector<uint8_t>& data, Padding padding) {
    size_t start = padding.start;
    size_t length = padding.length;
    if (length == 0) {
        std::cout << "  [No padding - EOI perfectly aligned with end of chunk]\n";
        return;
    }

    for (size_t i = 0; i < length; i += 16) {
        std::cout << std::format("  {:04x}: ", i);
        for (size_t j = 0; j < 16; ++j) {
            if (i + j < length) {
                std::cout << std::format("{:02x} ", data[start + i + j]);
            } else {
                std::cout << "   ";
            }
        }
        std::cout << "\n";
    }
}

void inspectPadding(const std::vector<uint8_t>& fileData) {
    int framesAnalyzed = 0;
    size_t i = 0;

    std::cout << "Scanning for padding robustly...\n\n";

    while (i < fileData.size() - 1) {
        if (fileData[i] == 0xff && fileData[i+1] == 0xd9) {
            size_t eoiPos = i;
            size_t paddingStart = eoiPos + 2;
            size_t nextHeaderPos = 0;

            for (size_t j = paddingStart; j < fileData.size() - 1; ++j) {
                if (fileData[j] == 0xaa && fileData[j+1] == 0xbb) {
                    nextHeaderPos = j;
                    break;
                }
            }

            if (nextHeaderPos > 0) {
                size_t paddingLength = nextHeaderPos - paddingStart;
                
                std::cout << "--- Frame " << framesAnalyzed << " End Found ---\n";
                std::cout << "EOI (FF D9) at: " << eoiPos << "\n";
                std::cout << "Next Header at: " << nextHeaderPos << "\n";
                std::cout << "Padding Size:   " << paddingLength << " bytes\n";
                
                printPaddingDump(fileData, {paddingStart, paddingLength});
                std::cout << "\n";
                
                framesAnalyzed++;

                i = nextHeaderPos; 
            } else {
                std::cout << "Found EOI at " << eoiPos << " but no subsequent header found (End of file?).\n";
                break;
            }

            if (framesAnalyzed >= 5) {
                break;
            }
        } else {
            i++;
        }
    }
}

void printHexDump(const std::vector<uint8_t>& data, DumpRange range) {
    size_t endPos = std::min(range.end, data.size());
    
    for (size_t i = range.start; i < endPos; i += 16) {
        // Print memory offset
        std::cout << std::format("{:08x}: ", i);
        
        // Print hex bytes
        for (size_t j = 0; j < 16; ++j) {
            if (i + j < endPos) {
                std::cout << std::format("{:02x} ", data[i + j]);
            } else {
                std::cout << "   "; // Pad incomplete lines
            }
        }
        std::cout << "\n";
    }
}

void inspectFrameBoundary(const std::vector<uint8_t>& fileData) {
    if (fileData.size() < 12) {
        std::cerr << "File too small.\n";
        return;
    }

    size_t nextAABB = 0;
    size_t firstFFD9 = 0;

    for (size_t i = 2; i < fileData.size() - 1; ++i) {
        if (nextAABB == 0 && fileData[i] == 0xaa && fileData[i+1] == 0xbb) {
            nextAABB = i;
        }

        if (firstFFD9 == 0 && fileData[i] == 0xff && fileData[i+1] == 0xd9) {
            firstFFD9 = i;
        }

        if (nextAABB > 0 && firstFFD9 > 0) {
            break;
        }
    }

    std::cout << "--- Ground Truth Analysis ---\n";
    std::cout << "First frame starts at: 0\n";
    std::cout << "Next Header (AA BB) found at offset: " << nextAABB << "\n";
    std::cout << "JPEG EOI (FF D9) found at offset:    " << firstFFD9 << "\n\n";

    if (nextAABB > 0 && nextAABB < firstFFD9) {
        std::cout << "CONCLUSION: The camera chunks frames.\n";
        std::cout << "It sends a header every " << nextAABB << " bytes.\n";
        std::cout << "You will need to strip headers from every chunk to assemble a full frame.\n";
    } 
    else if (firstFFD9 > 0 && firstFFD9 < nextAABB) {
        std::cout << "CONCLUSION: The camera sends full, unchunked frames.\n";
        std::cout << "The actual frame payload is " << firstFFD9 << " bytes.\n";
        std::cout << "Gap between EOI and next frame: " << (nextAABB - firstFFD9 - 2) << " bytes.\n";
    }
}

void extractFrames(const std::vector<uint8_t>& fileData) {
    std::cout << "Starting first-principles hardware extraction...\n\n";

    size_t i = 0;
    std::vector<uint8_t> currentFrame;
    currentFrame.reserve(ServerConstants::TWO_HUNDRED_FIFTY_SIX_KILOBYTES); // Pre-allocate 256KB buffer for safety
    
    int frameCount = 0;
    int lastFrameId = -1;

    const size_t TOTAL_HEADER_SIZE = sizeof(UsbPacketHeader) + sizeof(CameraPacketHeader);
    const uint16_t MAGIC_NUMBER = 0xBBAA; 

    while (i + TOTAL_HEADER_SIZE <= fileData.size()) {

        const UsbPacketHeader* header = reinterpret_cast<const UsbPacketHeader*>(&fileData[i]);

        if (header->getHeader() != MAGIC_NUMBER || (header->getCameraId() != 0x0B && header->getCameraId() != 0x07)) {
            i++;
            continue;
        }

        // --- THE PROXIMITY GHOST FILTER ---
        // The hardware leaks memory at 4KB boundaries, creating fake headers.
        // If we see another header within 300 bytes, this one is mathematically a ghost.
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
            i += nextHeaderOffset;
            continue;
        }

        size_t chunkTotalSize = sizeof(UsbPacketHeader) + header->getLength();
        if (i + chunkTotalSize > fileData.size()) {
            std::cout << "Reached incomplete hardware block at end of file. Stopping.\n";
            break; 
        }

        const CameraPacketHeader* meta = reinterpret_cast<const CameraPacketHeader*>(&fileData[i + sizeof(UsbPacketHeader)]);

        if (lastFrameId != -1 && meta->getFrameId() != lastFrameId) {
            if (!currentFrame.empty()) {

                size_t soiOffset = std::string::npos;
                size_t eoiOffset = std::string::npos;

                for (size_t j = 0; j + 1 < std::min<size_t>(256, currentFrame.size()); ++j) {
                    if (currentFrame[j] == 0xFF && currentFrame[j+1] == 0xD8) {
                        soiOffset = j;
                        break;
                    }
                }

                for (size_t j = currentFrame.size(); j >= 2; --j) {
                    if (currentFrame[j - 2] == 0xFF && currentFrame[j - 1] == 0xD9) {
                        eoiOffset = j;
                        break;
                    }
                }

                if (soiOffset != std::string::npos && eoiOffset != std::string::npos && soiOffset < eoiOffset) {
                    std::vector<uint8_t> cleanJpeg(currentFrame.begin() + soiOffset, currentFrame.begin() + eoiOffset);
                    
                    std::string filename = std::format("frame_{:04d}.jpg", frameCount++);
                    std::ofstream outJpg(filename, std::ios::binary);
                    outJpg.write(reinterpret_cast<const char*>(cleanJpeg.data()), cleanJpeg.size());
                    
                    std::cout << "[Success] Extracted " << filename << " (" << cleanJpeg.size() << " bytes)\n";
                } else {
                    std::cout << "[Warning] Discarded torn/corrupted frame buffer (No valid JPEG boundaries).\n";
                }
                
                currentFrame.clear();
            }
        }
        lastFrameId = meta->getFrameId();

        if (!meta->hasGravitySensor() && meta->getOtherFlags() == 0 && meta->getCameraNumber() < 2) {
            size_t payloadStart = i + TOTAL_HEADER_SIZE;
            size_t payloadSize = chunkTotalSize - TOTAL_HEADER_SIZE;
            
            currentFrame.insert(currentFrame.end(), 
                                fileData.begin() + payloadStart, 
                                fileData.begin() + payloadStart + payloadSize);
        }

        i += chunkTotalSize;
    }

    std::cout << "\nExtraction complete. Saved " << frameCount << " pristine frames.\n";
}

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

        extractFrames(fileData);

        return EXIT_SUCCESS;

    } catch (const std::exception& e) {
        std::cerr << "[Fatal Error] Unhandled exception in main: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
