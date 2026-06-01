#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#include "device_info.hpp"
#include "mjpeg_stream.hpp"
#include "server_time.hpp"
#include "usb_camera.hpp"
#include "wall_clock.hpp"
#include "shared_frame_pipeline.hpp"
#include "hardware_button_manager.hpp"
#include "web_server.hpp"

static constexpr int DEFAULT_PORT = 8080;
static MjpegStream* globalStream = nullptr;

void signalHandler(int signal) {
    std::cout << "\nSignal " << signal << " received. Initiating shutdown...\n";
    if (globalStream) {
        globalStream->stop();
    }
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
    std::cout << "  MJPEG Streaming Server Started\n";

    if (isUsingDefaultPort) {
        std::cout << "  -> Status: Running on DEFAULT port " << port << "\n";
        std::cout << "  -> Note:   To override this, specify a custom port value on launch.\n";
        std::cout << "             Example: " << argv[0] << " 9000\n";
    } else {
        std::cout << "  -> Status: Running on CUSTOM port override " << port << "\n";
    }
    
    std::cout << "  -> Web Dashboard:  http://localhost:" << port << "/web\n";
    std::cout << "  -> Raw Streaming (VLC):    http://localhost:" << port << "\n";
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

        // 1. Initialize Thread-Safe Core Atomic Token
        std::atomic<bool> running{true};

        // 2. Structural Layer Allocations
        WallClock systemClock;
        const ServerTime serverTime(systemClock, std::chrono::steady_clock::now());
        
        SharedFramePipeline pipeline;
        HardwareButtonManager buttonManager(serverTime);

        // 3. Initialize Web Server and Verify Port Bindings
        WebServer server(port, running, pipeline);
        if (!server.initialize()) {
            std::cerr << "[Fatal Exception] Failed to initialize web server socket bindings.\n";
            return EXIT_FAILURE;
        }

        // 4. Inject Dependencies into the Execution Runner Context
        MjpegStream stream(pipeline, buttonManager, server, running);
        globalStream = &stream;

        // 5. Fire Engine Up
        stream.run(camera);
        
    } catch (const std::exception& e) {
        std::cerr << "[Fatal] Unhandled exception: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
