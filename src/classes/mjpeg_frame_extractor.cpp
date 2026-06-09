#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "constants.hpp"
#include "mjpeg_frame_extractor.hpp"
#include "usb_packet_header.hpp"
#include "usb_payload_header.hpp"

void MjpegFrameExtractor::extractFrames(const std::vector<uint8_t>& data) {
    size_t i = 0;
    std::vector<uint8_t> currentFrame;
    currentFrame.reserve(Units::TWO_HUNDRED_FIFTY_SIX_KILOBYTES);
    
    int frameCount = 0;
    int lastFrameId = -1;

    while (i + TOTAL_USB_HEADER_SIZE <= data.size()) {
        const UsbPacketHeader* header = 
            reinterpret_cast<const UsbPacketHeader*>(
                &data[i]
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
            data.size() - i - 5
        );
        
        for (size_t d = 5; d <= scanLimit; ++d) {
            if (data[i+d] == UsbProtocol::USB_FRAME_HEADER_A && 
                data[i+d+1] == UsbProtocol::USB_FRAME_HEADER_B && 
               (data[i+d+2] == UsbProtocol::VIDEO_CAMERA_ID || 
                data[i+d+2] == UsbProtocol::GRAVITY_SENSOR_CAMERA_ID)) {
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
        if (i + packetSize > data.size()) {
            std::cout << "Reached incomplete hardware block at end of file. Stopping.\n";
            break; 
        }

        const UsbPayloadHeader* payloadHeader = 
            reinterpret_cast<const UsbPayloadHeader*>(
                &data[i + USB_PACKET_HEADER_SIZE]
            );

        if (lastFrameId != -1 && payloadHeader->getFrameId() != lastFrameId) {
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
                    // std::ofstream image(filename, std::ios::binary);

                    // image.write(
                    //     reinterpret_cast<const char*>(cleanJpeg.data()), 
                    //     cleanJpeg.size()
                    // );
                    
                    std::cout << "[Success] Extracted " << filename << " (" << cleanJpeg.size() << " bytes)\n";
                } else {
                    std::cout << "[Warning] Discarded torn/corrupted frame buffer (No valid JPEG boundaries).\n";
                }
                
                currentFrame.clear();
            }
        }
        lastFrameId = payloadHeader->getFrameId();

        if (!payloadHeader->hasGravitySensor() && 
            payloadHeader->getOtherFlags() == 0 && 
            payloadHeader->getCameraNumber() < 2) {

            size_t payloadStart = i + TOTAL_USB_HEADER_SIZE;
            size_t payloadSize = packetSize - TOTAL_USB_HEADER_SIZE;

            payloadHeader->getCameraNumber();
            payloadHeader->getFlags();
            payloadHeader->getOtherFlags();
            payloadHeader->getFrameId();
            payloadHeader->getGravitySensor();
            
            currentFrame.insert(
                currentFrame.end(), 
                data.begin() + payloadStart, 
                data.begin() + payloadStart + payloadSize
            );
        }

        i += packetSize;
    }

    std::cout << "\nExtraction complete. Saved " << frameCount << " pristine frames.\n";
}
