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

static std::atomic<bool> running{true};

static std::mutex frameMutex;
static ByteVector latestJpeg;
static uint32_t latestFrameId = 0;

static std::mutex snapshotMutex;
static ByteVector snapshotJpeg;
static std::atomic<bool> latchNextFrame{false};

static std::mutex buttonMutex;
static auto buttonPressStart = std::chrono::steady_clock::now();
static auto buttonLastSeen = std::chrono::steady_clock::now();
static bool buttonIsDepressed = false;

static void signalHandler(int) {
    running = false;
}

void processAndStoreFrame(const ByteVector& frame) {
    std::lock_guard<std::mutex> lock(frameMutex);
    
    size_t startOfImageOffset = std::string::npos;
    for (size_t index = 0; index + 1 < frame.size() && index < 32; ++index) {
        if (frame[index] == 0xFF && frame[index + 1] == 0xD8) {
            startOfImageOffset = index;
            break;
        }
    }

    if (startOfImageOffset != std::string::npos) {
        latestJpeg.assign(frame.begin() + startOfImageOffset, frame.end());
    } else {
        latestJpeg = frame;
    }

    latestFrameId++;

    if (latchNextFrame) {
        std::lock_guard<std::mutex> snapshotLock(snapshotMutex);
        snapshotJpeg = latestJpeg;
        latchNextFrame = false;
        std::cout << "[Server Core] Button Still-Frame Captured (" << snapshotJpeg.size() << " bytes)\n";
    }
}

void broadcastFrame(const ByteVector& frame) {
    if (frame.empty()) {
        return;
    }

    processAndStoreFrame(frame);
}

void hardwareButtonCallback() {
    auto currentTime = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(buttonMutex);

    if (!buttonIsDepressed || 
        std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - buttonLastSeen).count() > 200) {
        buttonPressStart = currentTime;
        buttonIsDepressed = true;
    }
    buttonLastSeen = currentTime;
}

void monitorButtonReleaseState() {
    auto currentTime = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(buttonMutex);

    if (buttonIsDepressed) {
        long long millisecondsSinceLastSeen = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - buttonLastSeen).count();
        if (millisecondsSinceLastSeen > 150) {
            long long totalPressDuration = std::chrono::duration_cast<std::chrono::milliseconds>(buttonLastSeen - buttonPressStart).count();
            buttonIsDepressed = false;

            if (totalPressDuration < 450) {
                std::cout << "[Button Filter] Quick press matching window (" << totalPressDuration << "ms). Capturing...\n";
                latchNextFrame = true;
            } else {
                std::cout << "[Button Filter] Long press lens toggle bypass (" << totalPressDuration << "ms).\n";
            }
        }
    }
}

void cameraCaptureLoop() {
    try {
        UsbCamera camera;
        UsbCameraProtocol protocol(
            [](const ByteVector &frame) { broadcastFrame(frame); },
            []() { hardwareButtonCallback(); }
        );

        std::cout << "[Hardware Engine] Pipeline operational.\n";

        ByteVector readBuffer;
        readBuffer.reserve(ServerConstants::ONE_MEGABYTE);

        while (running) {
            int returnStatus = camera.readFrame(readBuffer);
            if (returnStatus == 0) {
                protocol.handleFrame(readBuffer);
            } else if (returnStatus == LIBUSB_ERROR_NO_DEVICE) {
                std::cerr << "[Hardware Engine] Device disconnected.\n";
                running = false;
            }
            monitorButtonReleaseState();
        }
    } catch (const std::exception &exception) {
        std::cerr << "[Hardware Engine Exception]: " << exception.what() << "\n";
        running = false;
    }
}

int main(int argc, char* argv[]) {
    int chosenPort = DEFAULT_PORT;
    bool isUsingDefaultPort = true;

    if (argc > 1) {
        try {
            int parsedPort = std::stoi(argv[1]);
            if (parsedPort > 0 && parsedPort <= 65535) {
                chosenPort = parsedPort;
                isUsingDefaultPort = false;
            } else {
                std::cerr << "[Warning] Invalid network port range specified (" << argv[1] << "). Falling back to default port " << DEFAULT_PORT << ".\n";
            }
        } catch (const std::exception& exception) {
            std::cerr << "[Warning] Malformed network port parameter specified (" << argv[1] << "). Falling back to default port " << DEFAULT_PORT << ".\n";
        }
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGPIPE, SIG_IGN);

    latestJpeg.reserve(ServerConstants::ONE_MEGABYTE);
    snapshotJpeg.reserve(ServerConstants::ONE_MEGABYTE);

    WebServer server(
        chosenPort, 
        running, 
        frameMutex, 
        latestJpeg, 
        latestFrameId, 
        snapshotMutex, 
        snapshotJpeg
    );

    if (!server.initialize()) {
        std::cerr << "Failed to initialize async web server.\n";
        return 1;
    }

    std::cout << "==================================================================\n";
    std::cout << "  MJPEG Streaming Server Started\n";

    if (isUsingDefaultPort) {
        std::cout << "  -> Status: Running on DEFAULT port " << chosenPort << "\n";
        std::cout << "  -> Note:   To override this, specify a custom port value on launch.\n";
        std::cout << "             Example: " << argv[0] << " 9000\n";
    } else {
        std::cout << "  -> Status: Running on CUSTOM port override " << chosenPort << "\n";
    }
    
    std::cout << "  -> Web Dashboard:  http://localhost:" << chosenPort << "/web\n";
    std::cout << "  -> Raw Streaming (VLC):    http://localhost:" << chosenPort << "\n";
    std::cout << "==================================================================\n";

    std::thread cameraThread(cameraCaptureLoop);

    server.startEventLoop();

    if (cameraThread.joinable()) {
        cameraThread.join();
    }
    
    std::cout << "Application cleanly terminated.\n";
    return EXIT_SUCCESS;
}