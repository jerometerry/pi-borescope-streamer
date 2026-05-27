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

static constexpr uint8_t JPEG_SOI_MARKERS[] = { 0xFF, 0xD8 };

static constexpr int BUTTON_DEBOUNCE_TIME_MS = 200;
static constexpr int QUICK_PRESS_MIN_MS = 150;
static constexpr int QUICK_PRESS_MAX_MS = 450;
static constexpr int JPEG_SOI_MARKERS_MAX_POSITION = 32;
static constexpr int DEFAULT_PORT = 8080;

static std::atomic<bool> running{true};
static uint32_t frameId = 0;
static std::vector<uint8_t> frameBuffer;
static std::mutex frameMutex;
static std::vector<uint8_t> snapshotBuffer;
static std::mutex snapshotMutex;
static std::atomic<bool> snapshotNextFrame{false};
static std::mutex buttonMutex;
static auto buttonPressStart = std::chrono::steady_clock::now();
static auto buttonLastSeen = std::chrono::steady_clock::now();
static bool buttonIsDepressed = false;

void signalHandler(int signal);
void broadcastFrame(const std::vector<uint8_t>& frame);
void hardwareButtonCallback();
void monitorButtonReleaseState();
void startVideoFeed();

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

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGPIPE, SIG_IGN);

    frameBuffer.reserve(ServerConstants::ONE_MEGABYTE);
    snapshotBuffer.reserve(ServerConstants::ONE_MEGABYTE);

    WebServer server(
        port, 
        running, 
        frameId,
        frameBuffer,
        frameMutex, 
        snapshotBuffer,
        snapshotMutex
    );

    if (!server.initialize()) {
        std::cerr << "Failed to initialize async web server.\n";
        return 1;
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

    std::thread streamThread(startVideoFeed);

    server.start();

    if (streamThread.joinable()) {
        streamThread.join();
    }
    
    std::cout << "Application cleanly terminated.\n";
    return EXIT_SUCCESS;
}

void signalHandler(int signal) {
    std::cout << "\nSignal " << signal << " received. Initiating shutdown...\n";
    running = false;
}

void broadcastFrame(const std::vector<uint8_t>& frame) {
    // If the frame is empty, there's nothing to broadcast, so just return early. This can happen if the camera produces an empty frame for any reason, and we want to avoid broadcasting invalid data to clients.
    if (frame.empty()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(frameMutex);

        // Attempt to find the JPEG SOI markers within the first 32 bytes of the frame data.
        size_t offset = std::string::npos;
        for (size_t index = 0; index + 1 < frame.size() && index < JPEG_SOI_MARKERS_MAX_POSITION; ++index) {
            if (frame[index] == JPEG_SOI_MARKERS[0] && frame[index + 1] == JPEG_SOI_MARKERS[1]) {
                offset = index;
                break;
            }
        }

        // If the markers were found, discard any leading bytes before the SOI. This can help mitigate issues with certain cameras that prepend extraneous data before the JPEG frame. If the markers aren't found, just use the entire frame as-is.
        if (offset != std::string::npos) {
            frameBuffer.assign(frame.begin() + offset, frame.end());
        } else {
            frameBuffer = frame;
        }

        // Increment the frame ID to signal to any connected clients that a new frame is available. This happens after the frame buffer is updated to ensure clients can safely read the new frame data when they see the ID change.
        frameId++;

        // If a snapshot capture was requested by the hardware button, copy the latest frame to the snapshot buffer for serving to snapshot clients. This happens after the main frame buffer and ID are updated to ensure the snapshot captures the most recent frame.
        if (snapshotNextFrame) {
            std::lock_guard<std::mutex> snapshotLock(snapshotMutex);
            snapshotBuffer = frameBuffer;
            snapshotNextFrame = false;
            std::cout << "[Server Core] Button Still-Frame Captured (" << snapshotBuffer.size() << " bytes)\n";
        }
    }
}

void hardwareButtonCallback() {
    auto currentTime = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(buttonMutex);

    auto elapsed = currentTime - buttonLastSeen;
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    // If the button is currently not detected as pressed, or it has been detected as pressed but we haven't seen it still pressed for at least the debounce time, we can treat this as a new button press event. We then update the button press start time and set the depressed state to true. If the button is already depressed and we've seen it still pressed within the debounce time, we just update the last seen time to keep tracking how long it's been held down. This allows us to filter out noise and chatter from the button and only respond to legitimate presses.
    if (!buttonIsDepressed || elapsedMs > BUTTON_DEBOUNCE_TIME_MS) {
        buttonPressStart = currentTime;
        buttonIsDepressed = true;
    }
    buttonLastSeen = currentTime;
}

void checkForButtonQuickPress() {
    auto currentTime = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(buttonMutex);

    if (buttonIsDepressed) {
        auto elapsed = currentTime - buttonLastSeen;
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

        // If the button was previously detected as pressed but hasn't been seen as still pressed for a certain amount of time, we can infer that it was released. We then check how long the button was held down to determine if it qualifies as a "quick press" for snapshot capture or a long press for lens toggle (which we ignore, since a long press is a hardware event that toggles the lens).
        if (elapsedMs > QUICK_PRESS_MIN_MS) {
            auto duration = buttonLastSeen - buttonPressStart;
            auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
            buttonIsDepressed = false;

            if (durationMs < QUICK_PRESS_MAX_MS) {
                std::cout << "[Button Filter] Quick press matching window (" << durationMs << "ms). Capturing...\n";
                snapshotNextFrame = true;
            } else {
                std::cout << "[Button Filter] Long press lens toggle bypass (" << durationMs << "ms).\n";
            }
        }
    }
}

void startVideoFeed() {
    auto broadcastHandler = [](const std::vector<uint8_t>& frame) { 
        ::broadcastFrame(frame); 
    };

    auto buttonHandler = []() { 
        ::hardwareButtonCallback(); 
    };

    try {
        UsbCamera camera;
        UsbCameraProtocol protocol(broadcastHandler, buttonHandler);
        std::cout << "[Hardware Engine] Pipeline operational.\n";

        // Main capture loop: continuously read frames from the camera and pass them to the protocol handler for processing. If the camera is disconnected, libusb will return an error code which we check for to break the loop and initiate shutdown. We also call the button release state monitor on each iteration to check for any button release events that may have occurred since the last frame read.
        std::vector<uint8_t> readBuffer;
        readBuffer.reserve(ServerConstants::ONE_MEGABYTE);

        while (running) {
            int error = camera.readFrame(readBuffer);
            if (error == 0) {
                protocol.handleFrame(readBuffer);
            } else if (error == LIBUSB_ERROR_NO_DEVICE) {
                std::cerr << "[Hardware Engine] Device disconnected.\n";
                running = false;
            }
            checkForButtonQuickPress();
        }
    } catch (const std::exception &exception) {
        std::cerr << "[Hardware Engine Exception]: " << exception.what() << "\n";
        running = false;
    }
}