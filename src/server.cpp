/**
 * @file server.cpp
 * @brief The main entry point for the live video streaming application.
 * @details This is the primary program you run to actually use your camera. 
 * When executed, it automatically finds the camera on the USB bus, launches the 
 * video decoding engine, spins up the network broadcaster, and starts serving 
 * the live video feed to any web browser that connects to the Raspberry Pi's IP address.
 * 
 * By default, it broadcasts on port 8080, but you can override this by passing 
 * a different port number when you launch the program from the terminal.
 */

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include "device_info.hpp"
#include "mjpeg_frame_decoder.hpp"
#include "mjpeg_server.hpp"
#include "shared_frame_pipeline.hpp"
#include "usb_camera.hpp"
#include "usb_capture_engine.hpp"

namespace {
    constexpr int DEFAULT_PORT = 8080;
    std::atomic<bool> globalRunning{true};
}

void signalHandler(int signal) {
    std::cout << "\nSignal " << signal << " received. Initiating orderly engine shutdown...\n";
    globalRunning.store(false, std::memory_order_release);
}

int main(int argc, const char* argv[]) {
    int port = DEFAULT_PORT;
    bool isUsingDefaultPort = true;

    if (argc > 1) {
        try {
            int parsedPort = std::stoi(argv[1]);
            if (parsedPort > 0 && parsedPort <= 65535) {
                port = parsedPort;
                isUsingDefaultPort = false;
            } else {
                std::cerr << "[Warning] Invalid network port range specified (" << argv[1] << "). Falling back to default port " << DEFAULT_PORT << ".\n";
            }
        } catch (const std::exception& exception) {
            std::cerr << "[Warning] Malformed network port parameter specified (" << argv[1] << "). Falling back to default port " << DEFAULT_PORT << ".\n";
        }
    }

    std::cout << "==================================================================\n";
    std::cout << "  Pi-Borescope Streamer Started\n";

    if (isUsingDefaultPort) {
        std::cout << "  -> Status: Running on DEFAULT port " << port << "\n";
        std::cout << "  -> Note:   To override this, specify a custom port value on launch.\n";
        std::cout << "             Example: " << argv[0] << " 9000\n";
    } else {
        std::cout << "  -> Status: Running on CUSTOM port override " << port << "\n";
    }
    
    std::cout << "  -> Web Dashboard:          http://localhost:" << port << "/\n";
    std::cout << "  -> Raw Streaming (VLC):    http://localhost:" << port << "/stream\n";
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

        SharedFramePipeline pipeline;

        MjpegServer server(port, globalRunning, pipeline);

        MjpegFrameDecoder decoder([&pipeline](const std::vector<uint8_t>& frame) {
            auto buffer = pipeline.checkoutBuffer();
            if (buffer) {
                buffer->assign(frame.begin(), frame.end());
                pipeline.updateFrame(std::move(buffer));
            }
        });

        UsbCaptureEngine captureEngine([&decoder](std::span<const uint8_t> data) {
            decoder.processIncomingCameraData(data);
        }, globalRunning);


        std::cout << "[Server Core] Starting asynchronous capture and network worker engines...\n";

        captureEngine.start(camera);
        server.start();

        std::cout << "[Server Core] System fully operational. Awaiting network events.\n";
        
        while (globalRunning.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "[Server Core] Shutdown signal received. Stopping worker lanes...\n";

        captureEngine.stop();
        
    } catch (const std::exception& e) {
        std::cerr << "[Fatal] Unhandled exception in application core: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "[System Termination] All resources returned. Server exited cleanly.\n";
    return EXIT_SUCCESS;
}