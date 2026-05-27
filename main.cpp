#include "mjpeg_stream.hpp"
#include "usb_camera.hpp"
#include "usb_camera_protocol.hpp"
#include "web_server.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <mutex>
#include <thread>
#include <vector>

#include <libusb-1.0/libusb.h>

static constexpr int DEFAULT_PORT = 8080;
static MjpegStream stream;

void signalHandler(int signal) {
    std::cout << "\nSignal " << signal << " received. Initiating shutdown...\n";
    stream.stop();
}

int main(int argc, char* argv[]) {
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

    return stream.run(port);
}
