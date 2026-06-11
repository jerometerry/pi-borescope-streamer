/**
 * @file mjpeg_frame_extractor.cpp
 * @brief An offline tool to pull clean JPEG pictures out of raw camera dumps.
 */

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <vector>
#include "constants.hpp"
#include "usb_packet_header.hpp"
#include "usb_payload_header.hpp"

class FrameExtractor {
public:
    FrameExtractor(int imgOffset) : targetFrameNumber_(imgOffset) {
        currentFrame_.reserve(Units::TWO_HUNDRED_FIFTY_SIX_KILOBYTES);
        streamBuffer_.reserve(Units::ONE_MEGABYTE + Units::TWO_HUNDRED_FIFTY_SIX_KILOBYTES);
    }

    bool extractFrame(std::span<const uint8_t> newChunk) {
        streamBuffer_.insert(streamBuffer_.end(), newChunk.begin(), newChunk.end());

        size_t i = 0;
        bool targetFound = false;

        while (i + TOTAL_USB_HEADER_SIZE <= streamBuffer_.size()) {  
            const UsbPacketHeader* header = reinterpret_cast<const UsbPacketHeader*>(&streamBuffer_[i]);

            if (header->getHeader() != UsbProtocol::USB_FRAME_HEADER || 
                (header->getCameraId() != UsbProtocol::VIDEO_CAMERA_ID && 
                 header->getCameraId() != UsbProtocol::GRAVITY_SENSOR_CAMERA_ID)) {
                i++;
                continue;
            }

            bool isGhost = false;
            size_t nextHeaderOffset = 0;
            size_t scanLimit = std::min<size_t>(
                UsbProtocol::MAX_SCAN_LIMIT, 
                streamBuffer_.size() - i - 5
            );
            
            for (size_t d = 5; d <= scanLimit; ++d) {
                if (streamBuffer_[i+d] == UsbProtocol::USB_FRAME_HEADER_A && 
                    streamBuffer_[i+d+1] == UsbProtocol::USB_FRAME_HEADER_B && 
                   (streamBuffer_[i+d+2] == UsbProtocol::VIDEO_CAMERA_ID || 
                    streamBuffer_[i+d+2] == UsbProtocol::GRAVITY_SENSOR_CAMERA_ID)) {
                    isGhost = true;
                    nextHeaderOffset = d;
                    break;
                }
            }

            if (isGhost) {
                i += nextHeaderOffset;
                continue;
            }

            size_t packetSize = USB_PACKET_HEADER_SIZE + header->getLength();

            if (i + packetSize > streamBuffer_.size()) {
                break;
            }

            const UsbPayloadHeader* payloadHeader = 
                reinterpret_cast<const UsbPayloadHeader*>(&streamBuffer_[i + USB_PACKET_HEADER_SIZE]);

            if (lastFrameId_ != -1 && payloadHeader->getFrameId() != lastFrameId_) {
                if (!currentFrame_.empty()) {
                    size_t soiOffset = std::string::npos;
                    size_t eoiOffset = std::string::npos;
                    size_t maxSoiPosition = UsbProtocol::JPEG_SOI_MARKERS_MAX_POSITION;

                    for (size_t j = 0; j + 1 < std::min<size_t>(maxSoiPosition, currentFrame_.size()); ++j) {
                        if (currentFrame_[j] == UsbProtocol::BOUNDARY_MARKER && 
                            currentFrame_[j+1] == UsbProtocol::START_MARKER) {
                            soiOffset = j;
                            break;
                        }
                    }

                    for (size_t j = currentFrame_.size(); j >= 2; --j) {
                        if (currentFrame_[j - 2] == UsbProtocol::BOUNDARY_MARKER && 
                            currentFrame_[j - 1] == UsbProtocol::END_MARKER) {
                            eoiOffset = j;
                            break;
                        }
                    }

                    if (soiOffset != std::string::npos && eoiOffset != std::string::npos && soiOffset < eoiOffset) {
                        validFrameCount_++;

                        if (validFrameCount_ == targetFrameNumber_) {
                            std::vector<uint8_t> cleanJpeg(
                                currentFrame_.begin() + soiOffset, 
                                currentFrame_.begin() + eoiOffset
                            );
                            
                            std::string filename = std::format("frame_{:04d}.jpg", validFrameCount_);
                            std::ofstream image(filename, std::ios::binary);
                            image.write(reinterpret_cast<const char*>(cleanJpeg.data()), cleanJpeg.size());

                            std::cout << "[Success] Image extracted: " << filename << " (" << cleanJpeg.size() << " bytes)\n";
                            targetFound = true;
                        }
                    }
                    currentFrame_.clear();
                }
            }
            
            lastFrameId_ = payloadHeader->getFrameId();

            if (!payloadHeader->hasGravitySensor() && 
                payloadHeader->getOtherFlags() == 0 && 
                payloadHeader->getCameraNumber() < 2) {
                
                size_t payloadStart = i + TOTAL_USB_HEADER_SIZE;
                size_t payloadSize = packetSize - TOTAL_USB_HEADER_SIZE;
                
                currentFrame_.insert(
                    currentFrame_.end(), 
                    streamBuffer_.begin() + payloadStart, 
                    streamBuffer_.begin() + payloadStart + payloadSize
                );
            }

            i += packetSize;
            
            if (targetFound) {
                break;
            }
        }

        streamBuffer_.erase(streamBuffer_.begin(), streamBuffer_.begin() + i);

        return targetFound;
    }

    int getFrameCount() const { return validFrameCount_; }

private:
    int targetFrameNumber_;
    int validFrameCount_ = 0;
    int lastFrameId_ = -1;
    std::vector<uint8_t> streamBuffer_;
    std::vector<uint8_t> currentFrame_;
};


int main(int argc, const char* argv[]) {
    std::cout << std::unitbuf;

    try {
        if (argc != 3) {
            std::println(std::cerr, "Usage: {} <path_to_bin_file> <target_frame_number>", argv[0]);
            return EXIT_FAILURE;
        }

        std::string inputPath{argv[1]};
        const int64_t targetFrameNumber = std::stoll(argv[2]);

        if (targetFrameNumber < 1) {
            std::println(std::cerr, "[Error] Target frame number must be 1 or greater.");
            return EXIT_FAILURE;
        }

        std::ifstream file(inputPath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Error opening file!\n";
            return EXIT_FAILURE;
        }

        const size_t transferSize = Units::ONE_KILOBYTE;
        std::vector<uint8_t> buffer(transferSize);
        char* bufferPtr = reinterpret_cast<char*>(buffer.data());

        FrameExtractor extractor(targetFrameNumber);
        bool found = false;

        std::cout << "Scanning file: " << inputPath << " for frame #" << targetFrameNumber << "...\n";

        while (file.read(bufferPtr, transferSize) || file.gcount() > 0) {
            std::streamsize bytesRead = file.gcount();
            
            if (bytesRead > 0) {
                std::span<const uint8_t> chunk(buffer.data(), static_cast<size_t>(bytesRead));
                
                if (extractor.extractFrame(chunk)) {
                    found = true;
                    break;
                }
            }            
        }

        if (found) {
            std::println(std::cout, "Extraction complete. Target located gracefully.");
        } else {
            std::println(std::cerr, "\n[Finished] File processed completely.");
            std::println(std::cerr, "  -> Total Valid JPEGs Discovered: {}", extractor.getFrameCount()); 
            std::println(std::cerr, "  -> Error: Target frame #{} is out of bounds.", targetFrameNumber);
            return EXIT_FAILURE;
        }

    } catch (const std::exception& e) {
        std::cerr << "[Fatal Error] Terminated via unhandled exception: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
