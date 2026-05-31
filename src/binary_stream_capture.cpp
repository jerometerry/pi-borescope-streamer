#include <libusb.h>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#include <atomic>
#include <span>
#include "device_finder.hpp"
#include "device_info.hpp"
#include "server_constants.hpp"
#include "usb_camera.hpp"

// Global thread-safe atomic token to allow clean exiting via Ctrl+C
static std::atomic<bool> keepRunning{true};

void signalHandler(int /*signum*/) {
    keepRunning = false;
}

int main() {
    // Register signal handler to cleanly stop loops without corrupting file writes
    std::signal(SIGINT, signalHandler);

    try {
        // 1. Hardware Bus Discovery
        std::vector<DeviceInfo> cameras = DeviceFinder::superCameras();
        if (cameras.empty()) {
            std::cerr << "[Error] No Useeplus supercamera devices found on the USB bus.\n";
            return EXIT_FAILURE;
        }

        DeviceInfo cameraInfo = cameras[0];
        
        if (cameras.size() > 1) {
            std::cout << "Multiple Useeplus cameras detected:\n";
            for (size_t i = 0; i < cameras.size(); ++i) {
                std::cout << "  [" << i << "] Bus " << static_cast<int>(cameras[i].bus)
                          << " Address " << static_cast<int>(cameras[i].address)
                          << " - " << cameras[i].manufacturer << " " << cameras[i].product
                          << " (Serial: " << (cameras[i].serialNumber.empty() ? "N/A" : cameras[i].serialNumber) << ")\n";
            }
            
            size_t choice = 0;
            while (true) {
                std::cout << "\nSelect camera to stream [0-" << (cameras.size() - 1) << "]: ";
                if (std::cin >> choice && choice < cameras.size()) {
                    cameraInfo = cameras[choice];
                    break;
                }
                std::cout << "Invalid selection. Please try again.\n";
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            }
        }

        std::cout << "\n[Info] Binding stream to camera on Bus " << static_cast<int>(cameraInfo.bus)
                  << " Address " << static_cast<int>(cameraInfo.address) << "...\n";
        
        // 2. Open High-Performance Binary Output File 
        std::ofstream outFile("camera_stream.mjpeg", std::ios::out | std::ios::binary);
        if (!outFile.is_open()) {
            std::cerr << "[Fatal] Could not open output file for writing.\n";
            return EXIT_FAILURE;
        }

        // 3. Setup Hardware Aligned Buffer Space
        // Changed from 64KB down to exactly 4KB to line up with the hardware's internal alignment boundaries
        const size_t PACKET_PAGE_SIZE = ServerConstants::FOUR_KILOBYTES; 
        std::vector<uint8_t> buffer;
        buffer.reserve(PACKET_PAGE_SIZE); 

        int numBytes = 0;
        size_t totalBytesWritten = 0;

        std::cout << "Recording video stream to 'camera_stream.mjpeg'..." << "\n";
        std::cout << "Press Ctrl+C to stop.\n\n";

        // Initialize local camera object
        UsbCamera camera(cameraInfo);

        // 4. Main Low-Latency Capture Loop
        while (keepRunning.load(std::memory_order_relaxed)) {
            // Directly pass the aligned 4KB target chunk size boundary
            int status = camera.read(1, buffer, PACKET_PAGE_SIZE, numBytes);

            if (status == 0 && numBytes > 0) {
                // Write the exact slice received without flushing stream overhead unnecessarily
                outFile.write(reinterpret_cast<const char*>(buffer.data()), numBytes);
                totalBytesWritten += numBytes;
            } 
            else if (status != 0) {
                // Ignore timeouts gracefully—this is normal behavior if the bus is idle
                if (status == LIBUSB_ERROR_TIMEOUT) {
                    continue; 
                } 
                if (status == LIBUSB_ERROR_NO_DEVICE) {
                    std::cerr << "\n[Error] Camera physically disconnected from USB bus.\n";
                    break;
                }
                std::cerr << "\n[Critical] USB Read Error: " << libusb_error_name(status) << "\n";
                break;
            }
        }

        // 5. Cleanup and Flush
        std::cout << "\nRecording stopped. Syncing disk buffers...\n";
        outFile.close();
        std::cout << "Done! Total bytes saved: " << totalBytesWritten << " bytes.\n";

    } catch (const std::exception& e) {
        std::cerr << "[Fatal] Unhandled exception: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
