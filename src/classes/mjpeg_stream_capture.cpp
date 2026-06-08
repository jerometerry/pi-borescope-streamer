#include <libusb.h>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>
#include <atomic>
#include "binary_stream_capture.hpp"
#include "constants.hpp"
#include "usb_camera.hpp"
struct DeviceInfo;

int BinaryStreamCapture::capture(const std::atomic<bool>& running, const DeviceInfo& cameraInfo) {
	std::ofstream outFile("camera_stream.mjpeg", std::ios::out | std::ios::binary);
	if (!outFile.is_open()) {
		std::cerr << "[Fatal] Could not open output file for writing.\n";
		return EXIT_FAILURE;
	}

	const size_t PACKET_PAGE_SIZE = Units::FOUR_KILOBYTES; 
	std::vector<uint8_t> buffer;
	buffer.reserve(PACKET_PAGE_SIZE); 

	int numBytes = 0;
	size_t totalBytesWritten = 0;

	std::cout << "Recording video stream to 'camera_stream.mjpeg'..." << "\n";
	std::cout << "Press Ctrl+C to stop.\n\n";

	UsbCamera camera(cameraInfo);

	while (running.load(std::memory_order_relaxed)) {
		int status = camera.read(1, buffer, PACKET_PAGE_SIZE, numBytes);

		if (status == 0 && numBytes > 0) {
			outFile.write(reinterpret_cast<const char*>(buffer.data()), numBytes);
			totalBytesWritten += numBytes;
		} 
		else if (status != 0) {
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

	std::cout << "\nRecording stopped. Syncing disk buffers...\n";
	outFile.close();
	std::cout << "Done! Total bytes saved: " << totalBytesWritten << " bytes.\n";
	return EXIT_SUCCESS;
}
