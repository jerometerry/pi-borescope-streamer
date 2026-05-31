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
#include "device_finder.hpp"
#include "device_info.hpp"
#include "server_constants.hpp"
#include "usb_camera.hpp"

// Global flag to allow clean exiting via Ctrl+C
volatile sig_atomic_t keepRunning = 1;

void signalHandler(int signum) {
    keepRunning = 0;
}

int main() {
    // Register signal handler to cleanly close the file stream
    std::signal(SIGINT, signalHandler);

	try {

		std::vector<DeviceInfo> cameras = DeviceFinder::superCameras();
        if (cameras.empty()) {
            std::cerr << "No Useeplus supercamera devices found on the USB bus.\n";
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
		
		// Note: You will likely need to call whatever initialization/open methods 
		// your class has here, e.g., camera.connect() or camera.open()

		// 2. Open binary output file
		std::ofstream outFile("camera_stream.mjpeg", std::ios::out | std::ios::binary);
		if (!outFile.is_open()) {
			std::cerr << "Error: Could not open output file for writing." << "\n";
			return EXIT_FAILURE;
		}

		// 3. Setup Buffer
		// 64KB is a standard, efficient chunk size for USB bulk transfers
		const size_t MAX_BUFFER_SIZE = ServerConstants::SIXTY_FOUR_KILOBYTES; 
		std::vector<uint8_t> buffer;
		
		// Pre-allocate capacity so your read method's buffer.capacity() check works correctly
		buffer.reserve(MAX_BUFFER_SIZE); 

		int numBytes = 0;
		size_t totalBytesWritten = 0;

		std::cout << "Recording video stream to 'camera_stream.mjpeg'..." << "\n";
		std::cout << "Press Ctrl+C to stop." << "\n";

		UsbCamera camera(cameraInfo);

		// 4. Main Capture Loop
		while (keepRunning) {
			int status = camera.read(1, buffer, MAX_BUFFER_SIZE, numBytes);

			if (status == 0 && numBytes > 0) {
				// Write the exact bytes read to the file. 
				// Because your read() resizes the buffer, buffer.size() == numBytes
				outFile.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
				totalBytesWritten += buffer.size();
			} 
			else if (status != 0) {
				// Check for timeouts. USB bulk transfers often time out if the camera 
				// has no new frame data yet. We usually want to ignore timeouts and keep trying.
				if (status == LIBUSB_ERROR_TIMEOUT) {
					continue; 
				} else {
					std::cerr << "\nCritical USB Read Error: " << libusb_error_name(status) << "\n";
					break;
				}
			}
		}

		// 5. Cleanup
		std::cout << "\nRecording stopped. Total bytes written: " << totalBytesWritten << "\n";
		outFile.close();

    } catch (const std::exception& e) {
        std::cerr << "[Fatal] Unhandled exception: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}