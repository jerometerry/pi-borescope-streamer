#include "device_finder.hpp"
#include <csignal>
#include <iostream>
#include <string>

void signalHandler(int signal) {
    std::cout << "\nSignal " << signal << " received. Initiating shutdown...\n";
}

int main() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGPIPE, SIG_IGN);

    try {
        std::vector<DeviceInfo> devices = DeviceFinder::listDevices(false);
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
