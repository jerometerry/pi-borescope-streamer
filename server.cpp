#include <csignal>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <string>

#include <libusb-1.0/libusb.h>

#include "borescope_device.hpp"
#include "borescope_stream_decoder.hpp"
#include "borescope_web_server.hpp"

static std::atomic<bool> globalRunning{true};

static std::mutex frameMutex;
static std::condition_variable frameConditionVariable;
static byteVector latestJpeg;
static uint32_t latestFrameId = 0;

static std::mutex snapshotMutex;
static byteVector snapshotJpeg;
static std::atomic<bool> latchNextFrame{false};

static std::mutex buttonMutex;
static auto buttonPressStart = std::chrono::steady_clock::now();
static auto buttonLastSeen = std::chrono::steady_clock::now();
static bool buttonIsDepressed = false;

static void signalHandler(int) {
    globalRunning = false;
    frameConditionVariable.notify_all();
}

void processAndStoreFrame(const byteVector& frame) {
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

void broadcastFrame(const byteVector& frame) {
    if (frame.empty()) {
        return;
    }

    processAndStoreFrame(frame);

    frameConditionVariable.notify_all();
}

void hardwareButtonCallback() {
    auto currentTime = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(buttonMutex);

    if (!buttonIsDepressed || std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - buttonLastSeen).count() > 200) {
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
        BorescopeDevice hardwarePipe;
        BorescopeStreamDecoder protocolParser(
            [](const byteVector &frame) { broadcastFrame(frame); },
            []() { hardwareButtonCallback(); }
        );

        std::cout << "[Hardware Engine] Pipeline operational.\n";

        byteVector readBuffer;
        readBuffer.reserve(ServerConstants::ONE_MEGABYTE);

        while (globalRunning) {
            int returnStatus = hardwarePipe.readFrame(readBuffer);
            if (returnStatus == 0) {
                protocolParser.handleUseeplusFrame(readBuffer);
            } else if (returnStatus == LIBUSB_ERROR_NO_DEVICE) {
                std::cerr << "[Hardware Engine] Device disconnected.\n";
                globalRunning = false;
                frameConditionVariable.notify_all();
            }
            monitorButtonReleaseState();
        }
    } catch (const std::exception &exception) {
        std::cerr << "[Hardware Engine Exception]: " << exception.what() << "\n";
        globalRunning = false;
        frameConditionVariable.notify_all();
    }
}

int main(int argc, char* argv[]) {
    constexpr int defaultPort = 8080;
    int chosenPort = defaultPort;
    bool isUsingDefaultPort = true;

    if (argc > 1) {
        try {
            int parsedPort = std::stoi(argv[1]);
            if (parsedPort > 0 && parsedPort <= 65535) {
                chosenPort = parsedPort;
                isUsingDefaultPort = false;
            } else {
                std::cerr << "[Warning] Invalid network port range specified (" << argv[1] << "). Falling back to default port " << defaultPort << ".\n";
            }
        } catch (const std::exception& exception) {
            std::cerr << "[Warning] Malformed network port parameter specified (" << argv[1] << "). Falling back to default port " << defaultPort << ".\n";
        }
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGPIPE, SIG_IGN);

    latestJpeg.reserve(ServerConstants::ONE_MEGABYTE);
    snapshotJpeg.reserve(ServerConstants::ONE_MEGABYTE);

    BorescopeWebServer webServer(
        chosenPort, 
        globalRunning, 
        frameMutex, 
        latestJpeg, 
        latestFrameId, 
        snapshotMutex, 
        snapshotJpeg
    );

    if (!webServer.initialize()) {
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

    webServer.startEventLoop();

    if (cameraThread.joinable()) {
        cameraThread.join();
    }
    
    std::cout << "Application cleanly terminated.\n";
    return EXIT_SUCCESS;
}