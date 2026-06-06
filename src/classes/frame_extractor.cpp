#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "constants.hpp"
#include "data_structures.hpp"
#include "frame_extractor.hpp"

void FrameExtractor::printPaddingDump(const std::vector<uint8_t>& data, Padding padding) {
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

void FrameExtractor::inspectPadding(const std::vector<uint8_t>& fileData) {
    int framesAnalyzed = 0;
    size_t i = 0;

    std::cout << "Scanning for padding robustly...\n\n";

    while (i < fileData.size() - 1) {
        if (fileData[i] == UsbProtocol::BOUNDARY_MARKER && fileData[i+1] == UsbProtocol::END_MARKER) {
            size_t eoiPos = i;
            size_t paddingStart = eoiPos + 2;
            size_t nextHeaderPos = 0;

            for (size_t j = paddingStart; j < fileData.size() - 1; ++j) {
                if (fileData[j] == UsbProtocol::USB_FRAME_HEADER_A && 
                    fileData[j+1] == UsbProtocol::USB_FRAME_HEADER_B) {
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

void FrameExtractor::printHexDump(const std::vector<uint8_t>& data, DumpRange range) {
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

void FrameExtractor::inspectFrameBoundary(const std::vector<uint8_t>& fileData) {
    if (fileData.size() < 12) {
        std::cerr << "File too small.\n";
        return;
    }

    size_t nextHeaderMarker = 0;
    size_t firstEoiMarker = 0;

    for (size_t i = 2; i < fileData.size() - 1; ++i) {
        if (nextHeaderMarker == 0 && 
                fileData[i] == UsbProtocol::USB_FRAME_HEADER_A && 
                fileData[i+1] == UsbProtocol::USB_FRAME_HEADER_B) {
            nextHeaderMarker = i;
        }

        if (firstEoiMarker == 0 && 
                fileData[i] == UsbProtocol::BOUNDARY_MARKER && 
                fileData[i+1] == UsbProtocol::END_MARKER) {
            firstEoiMarker = i;
        }

        if (nextHeaderMarker > 0 && firstEoiMarker > 0) {
            break;
        }
    }

    std::cout << "--- Ground Truth Analysis ---\n";
    std::cout << "First frame starts at: 0\n";
    std::cout << "Next Header (AA BB) found at offset: " << nextHeaderMarker << "\n";
    std::cout << "JPEG EOI (FF D9) found at offset:    " << firstEoiMarker << "\n\n";

    if (nextHeaderMarker > 0 && nextHeaderMarker < firstEoiMarker) {
        std::cout << "CONCLUSION: The camera chunks frames.\n";
        std::cout << "It sends a header every " << nextHeaderMarker << " bytes.\n";
        std::cout << "You will need to strip headers from every chunk to assemble a full frame.\n";
    } 
    else if (firstEoiMarker > 0 && firstEoiMarker < nextHeaderMarker) {
        std::cout << "CONCLUSION: The camera sends full, unchunked frames.\n";
        std::cout << "The actual frame payload is " << firstEoiMarker << " bytes.\n";
        std::cout << "Gap between EOI and next frame: " << (nextHeaderMarker - firstEoiMarker - 2) << " bytes.\n";
    }
}

void FrameExtractor::extractFrames(const std::vector<uint8_t>& fileData) {
    std::cout << "Starting first-principles hardware extraction...\n\n";

    size_t i = 0;
    std::vector<uint8_t> currentFrame;
    currentFrame.reserve(Units::TWO_HUNDRED_FIFTY_SIX_KILOBYTES); // Pre-allocate 256KB buffer for safety
    
    int frameCount = 0;
    int lastFrameId = -1;

    while (i + USB::TotalHeaderSize <= fileData.size()) {

        const USB::PacketHeader* header = 
            reinterpret_cast<const USB::PacketHeader*>(
                &fileData[i]
            );

        if (header->getHeader() != UsbProtocol::USB_FRAME_HEADER || 
            (header->getCameraId() != UsbProtocol::VIDEO_CAMERA_ID && 
             header->getCameraId() != UsbProtocol::GRAVITY_SENSOR_CAMERA_ID)) {
            i++;
            continue;
        }

        // --- THE PROXIMITY GHOST FILTER ---
        // The hardware leaks memory at 4KB boundaries, creating fake headers.
        // If we see another header within 300 bytes, this one is mathematically a ghost.
        bool isGhost = false;
        size_t nextHeaderOffset = 0;
        size_t scanLimit = std::min<size_t>(
            UsbProtocol::MAX_SCAN_LIMIT, 
            fileData.size() - i - 5
        );
        
        for (size_t d = 5; d <= scanLimit; ++d) {
            if (fileData[i+d] == UsbProtocol::USB_FRAME_HEADER_A && 
                fileData[i+d+1] == UsbProtocol::USB_FRAME_HEADER_B && 
               (fileData[i+d+2] == UsbProtocol::VIDEO_CAMERA_ID || 
                fileData[i+d+2] == UsbProtocol::GRAVITY_SENSOR_CAMERA_ID)) {
                isGhost = true;
                nextHeaderOffset = d;
                break;
            }
        }

        if (isGhost) {
            i += nextHeaderOffset;
            continue;
        }

        size_t packetSize = USB::PacketHeaderSize + header->getLength();
        if (i + packetSize > fileData.size()) {
            std::cout << "Reached incomplete hardware block at end of file. Stopping.\n";
            break; 
        }

        const USB::PayloadHeader* meta = 
            reinterpret_cast<const USB::PayloadHeader*>(
                &fileData[i + USB::PacketHeaderSize]
            );

        if (lastFrameId != -1 && meta->getFrameId() != lastFrameId) {
            if (!currentFrame.empty()) {

                size_t soiOffset = std::string::npos;
                size_t eoiOffset = std::string::npos;
                size_t maxSoiPosition = UsbProtocol::JPEG_SOI_MARKERS_MAX_POSITION;

                for (size_t j = 0; j + 1 < std::min<size_t>(maxSoiPosition, currentFrame.size()); ++j) {
                    if (currentFrame[j] == UsbProtocol::BOUNDARY_MARKER && 
                        currentFrame[j+1] == UsbProtocol::START_MARKER) {
                        soiOffset = j;
                        break;
                    }
                }

                for (size_t j = currentFrame.size(); j >= 2; --j) {
                    if (currentFrame[j - 2] == UsbProtocol::BOUNDARY_MARKER && 
                        currentFrame[j - 1] == UsbProtocol::END_MARKER) {
                        eoiOffset = j;
                        break;
                    }
                }

                if (soiOffset != std::string::npos && eoiOffset != std::string::npos && soiOffset < eoiOffset) {
                    std::vector<uint8_t> cleanJpeg(
                        currentFrame.begin() + soiOffset, 
                        currentFrame.begin() + eoiOffset
                    );
                    
                    std::string filename = std::format("frame_{:04d}.jpg", frameCount++);
                    std::ofstream image(filename, std::ios::binary);

                    image.write(
                        reinterpret_cast<const char*>(cleanJpeg.data()), 
                        cleanJpeg.size()
                    );
                    
                    std::cout << "[Success] Extracted " << filename << " (" << cleanJpeg.size() << " bytes)\n";
                } else {
                    std::cout << "[Warning] Discarded torn/corrupted frame buffer (No valid JPEG boundaries).\n";
                }
                
                currentFrame.clear();
            }
        }
        lastFrameId = meta->getFrameId();

        if (!meta->hasGravitySensor() && meta->getOtherFlags() == 0 && meta->getCameraNumber() < 2) {
            size_t payloadStart = i + USB::TotalHeaderSize;
            size_t payloadSize = packetSize - USB::TotalHeaderSize;
            
            currentFrame.insert(
                currentFrame.end(), 
                fileData.begin() + payloadStart, 
                fileData.begin() + payloadStart + payloadSize
            );
        }

        i += packetSize;
    }

    std::cout << "\nExtraction complete. Saved " << frameCount << " pristine frames.\n";
}