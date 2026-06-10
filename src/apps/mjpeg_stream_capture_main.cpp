/**
 * @file binary_stream_capture.cpp
 * @brief A hardcore debugging tool that rips raw data straight off the USB cable to a file.
 * @details This tool completely bypasses the video decoder and network server. It connects 
 * to the camera, wakes it up, and blindly dumps every single byte of data coming across 
 * the wire into a `.mjpeg` file on the hard drive. 
 * 
 * This is incredibly useful for reverse engineering. If a camera starts sending weird 
 * glitchy data that crashes the decoder, a developer can run this tool to save a "pure" 
 * recording of the glitch, which they can then analyze later to figure out what went wrong.
 */

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>
#include "buffer.hpp"
#include "buffer_pool.hpp"
#include "buffer_ptr.hpp"
#include "constants.hpp"
#include "disruptor.hpp"
#include "hardware_buffer.hpp"
#include "intrusive_ptr.hpp"
#include "usb_device_finder.hpp"
#include "usb_device_info.hpp"
#include "usb_driver.hpp"

namespace {
    static std::atomic<bool> running{true};
}

void signalHandler(int /*signum*/) {
    running = false;
}

bool selectCamera(UsbDeviceInfo& cameraInfo) {
	std::vector<UsbDeviceInfo> cameras = UsbDeviceFinder::superCameras();
	if (cameras.empty()) {
		std::cerr << "[Error] No Useeplus supercamera devices found on the USB bus.\n";
		return false;
	}

	cameraInfo = cameras[0];
	
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

	return true;
}

int main() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGPIPE, SIG_IGN);

    try {
        UsbDeviceInfo camera;
        
        if (!selectCamera(camera)) {
            return EXIT_FAILURE;
        }

        std::cout << "\n[Info] Binding stream to camera on Bus " << static_cast<int>(camera.bus)
                  << " Address " << static_cast<int>(camera.address) << "...\n";

		disruptor::Disruptor<HardwareBuffer, 65536> ringBuffer;
		for (int64_t i = 0; i < 65536; i++) {
			ringBuffer.getBySequence(i).pre_allocate(Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);
		}

		std::jthread diskWriter([&ringBuffer](const std::stop_token& st) {
            std::ofstream outFile("camera_stream.mjpeg", std::ios::out | std::ios::binary);
            int64_t next_read = 0;
            
            while (!st.stop_requested()) {
				int64_t available = ringBuffer.waitFor(next_read);

				while (next_read <= available) {
					HardwareBuffer& slot = ringBuffer.getBySequence(next_read);

					if (slot.active_size > 0) {
						outFile.write(reinterpret_cast<const char*>(slot.storage.data()), slot.active_size);
					}

					next_read++;
				}
				ringBuffer.markConsumed(next_read - 1);
			}
			ringBuffer.markConsumed(next_read -1);
        });

		auto transfer = [&](UsbTransferStatus status, std::span<const uint8_t> payload) -> bool {
			static int call_count = 0;
    		if (call_count++ % 100 == 0) { 
				std::cerr << "[DEBUG] Transfer callback fired. Status: " 
						  << (int)status << " Size: " << payload.size() << "\n";
			}

			if (status == UsbTransferStatus::Completed && !payload.empty()) {
				int64_t seq = ringBuffer.claim();
				HardwareBuffer& slot = ringBuffer.getBySequence(seq);
				slot.write_payload(payload);
				ringBuffer.publish(seq);
            }
            return status != UsbTransferStatus::Disconnected; 
        };

        UsbDriver driver(transfer, &running);

        std::cout << "[Server Core] Starting asynchronous capture and network worker engines...\n";

        driver.start(camera);

        std::cout << "[Server Core] System fully operational. Awaiting network events.\n";
        
        while (running.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "[Server Core] Shutdown signal received. Stopping worker lanes...\n";

        driver.stop();
		int64_t seq = ringBuffer.claim();
		ringBuffer.getBySequence(seq) = nullptr;
		ringBuffer.publish(seq);

		return EXIT_SUCCESS;

    } catch (const std::exception& e) {
        std::cerr << "[Fatal] Unhandled exception: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
