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
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "mjpeg_stream_capture.hpp"
#include "usb_device_finder.hpp"
#include "usb_device_info.hpp"

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
            std::cout << "  [" << i << "] Bus " << static_cast<int>(cameras[i].bus) << " Address "
                      << static_cast<int>(cameras[i].address) << " - " << cameras[i].manufacturer
                      << " " << cameras[i].product << " (Serial: "
                      << (cameras[i].serialNumber.empty() ? "N/A" : cameras[i].serialNumber)
                      << ")\n";
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

    try {
        UsbDeviceInfo cameraInfo;

        if (!selectCamera(cameraInfo)) {
            return EXIT_FAILURE;
        }

        std::cout << "\n[Info] Binding stream to camera on Bus " << static_cast<int>(cameraInfo.bus)
                  << " Address " << static_cast<int>(cameraInfo.address) << "...\n";

        return MjpegStreamCapture::capture(running, cameraInfo);

    } catch (const std::exception& e) {
        std::cerr << "[Fatal] Unhandled exception: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}