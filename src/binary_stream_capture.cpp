#include <libusb.h>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>
#include "device_finder.hpp"
#include "device_info.hpp"
#include "server_constants.hpp"
#include "usb_camera.hpp"
#include "usb_frame_decoder.hpp"

static bool running = true;
static std::mutex frameMutex;
static uint32_t frameId = 0;
static const std::string DUMP_FILE = "raw_camera_dump.bin";

void signalHandler(int signal) {
    std::cout << "\nSignal " << signal << " received. Initiating shutdown...\n";
    running = false;
}

void startVideoFeed(const DeviceInfo& target) {
    auto broadcastHandler = [](const std::vector<uint8_t>& frame) { 
        if (frame.empty()) {
            return;
        }

        {
            std::scoped_lock<std::mutex> lock(frameMutex);
            frameId++;
        }
    };
    
    auto buttonHandler = []() {};

    try {
        UsbCamera camera(target);
        UsbFrameDecoder decoder(broadcastHandler, buttonHandler);
        
        // Open the binary dump file
        std::ofstream dumpFile(DUMP_FILE.data(), std::ios::binary);
        if (!dumpFile) {
            std::cerr << "[Error] Failed to open " << DUMP_FILE << " for writing.\n";
            return;
        }

        std::cout << "[Hardware Engine] Pipeline operational. Writing raw URB stream to disk...\n";

        std::vector<uint8_t> readBuffer;
        readBuffer.reserve(ServerConstants::ONE_MEGABYTE);

        while (running) {
            int error = camera.read(readBuffer);
            if (error == 0) {
                // EXACT HARDWARE MIRROR: Write the raw libusb buffer (including the 80-byte ghost padding) 
                // directly to disk before the decoder touches it.
                dumpFile.write(reinterpret_cast<const char*>(readBuffer.data()), readBuffer.size());
                
                decoder.processIncomingCameraData(std::span<const uint8_t>{readBuffer});
            } else if (error == LIBUSB_ERROR_NO_DEVICE) {
                std::cerr << "[Hardware Engine] Device disconnected.\n";
                running = false;
            }
        }
    } catch (const std::exception &exception) {
        std::cerr << "[Hardware Engine Exception]: " << exception.what() << "\n";
        running = false;
    }
}

int main() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGPIPE, SIG_IGN);

    try {
        std::vector<DeviceInfo> cameras = DeviceFinder::all();
        if (cameras.empty()) {
            std::cerr << "No Useeplus supercamera devices found on the USB bus.\n";
            return EXIT_FAILURE;
        }

        DeviceInfo camera = cameras[0];
        
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
                    camera = cameras[choice];
                    break;
                }
                std::cout << "Invalid selection. Please try again.\n";
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            }
        }

        std::cout << "\n[Info] Binding stream to camera on Bus " << static_cast<int>(camera.bus)
                  << " Address " << static_cast<int>(camera.address) << "...\n";

        std::thread streamThread(startVideoFeed, camera);

        if (streamThread.joinable()) {
            streamThread.join();
        }        
    } catch (const std::exception& e) {
        std::cerr << "[Fatal] Unhandled exception: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}