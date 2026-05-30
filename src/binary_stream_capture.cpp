#include "usb_camera.hpp"
#include "wall_clock.hpp"
#include "device_info.hpp"
#include "server_time.hpp"
#include "usb_camera.hpp"
#include "usb_frame_decoder.hpp"
#include "server_constants.hpp"

#include <chrono>
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

#include <libusb.h>

static bool running = true;
static std::mutex frameMutex;
static uint32_t frameId = 0;
static char DUMP_FILE[] = "raw_camera_dump.bin";

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
    auto buttonHandler = []() { 
    };

    try {
        UsbCamera camera(target);
        UsbFrameDecoder decoder(broadcastHandler, buttonHandler);
        std::cout << "[Hardware Engine] Pipeline operational.\n";

        std::cout << "[Debug] Opening binary stream dump: raw_camera_dump.bin\n";
        std::ofstream rawDump(DUMP_FILE, std::ios::binary);

        std::vector<uint8_t> readBuffer;
        readBuffer.reserve(ServerConstants::ONE_MEGABYTE);

        while (running) {
            int error = camera.read(readBuffer);
            if (error == 0) {
                if (rawDump.is_open()) {
                    rawDump.write(reinterpret_cast<const char*>(readBuffer.data()), readBuffer.size());
                    rawDump.flush();
                }
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
    std::cout << "==================================================================\n";
    std::cout << "  Binary Stream Started\n";
    std::cout << "  File: " << DUMP_FILE << "\n";
    std::cout << "==================================================================\n";

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGPIPE, SIG_IGN);

    try {
        std::vector<DeviceInfo> cameras = UsbCamera::listCameras();
        if (cameras.empty()) {
            std::cerr << "[Fatal] No Useeplus supercamera devices found on the USB bus.\n";
            return EXIT_FAILURE;
        }

        DeviceInfo camera = cameras.front();

        if (cameras.size() > 1) {
            std::cout << "\nMultiple cameras detected:\n";
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

        WallClock systemClock;
        const ServerTime serverTime(systemClock, std::chrono::steady_clock::now());

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

void stop() {
    running = false;
}


