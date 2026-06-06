/**
 * @file attached_usb_devices.cpp
 * @brief A diagnostic tool to scan the Raspberry Pi's USB ports for compatible cameras.
 * @details Before a user tries to start the main streaming server, they can run this 
 * simple utility to verify that their camera is physically plugged in, turned on, 
 * and recognized by the operating system. It prints out a clean list of every 
 * compatible endoscope it finds, including the hardware Bus and Address numbers.
 */

#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <vector>
#include "device_finder.hpp"
#include "device_info.hpp"

void signalHandler(int signal) {
    std::cout << "\nSignal " << signal << " received. Initiating shutdown...\n";
}

int main() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGPIPE, SIG_IGN);

    try {
        std::vector<DeviceInfo> devices = DeviceFinder::all();
        if (devices.empty()) {
            std::cerr << "No Useeplus supercamera devices found on the USB bus.\n";
        } else {
            for (size_t i = 0; i < devices.size(); ++i) {
                std::cout << "  [" << i << "] Bus " << static_cast<int>(devices[i].bus)
                            << " Address " << static_cast<int>(devices[i].address)
                            << " - Is SuperCamera: " << (devices[i].isSuperCamera ? "Yes" : "No")
                            << " - " << devices[i].manufacturer << " " << devices[i].product
                            << " (Serial: " << (devices[i].serialNumber.empty() ? "N/A" : devices[i].serialNumber) 
                            << ")\n";
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[Fatal] Unhandled exception: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
