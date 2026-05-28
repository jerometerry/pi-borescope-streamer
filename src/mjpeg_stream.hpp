#pragma once

#include "camera_info.hpp"
#include <atomic>
#include <chrono>
#include <csignal>
#include <mutex>
#include <vector>

class MjpegStream {

public:
    MjpegStream();
    ~MjpegStream();

    MjpegStream(const MjpegStream&) = delete;
    MjpegStream& operator=(const MjpegStream&) = delete;
    MjpegStream(MjpegStream&&) = delete;
    MjpegStream& operator=(MjpegStream&&) = delete;

    int run(int port, const CameraInfo& target);
    void stop();

private:
    void broadcastFrame(const std::vector<uint8_t>& frame);
    void hardwareButtonCallback();
    void checkForButtonQuickPress();
    void startVideoFeed(const CameraInfo& target);

    static constexpr uint8_t JPEG_SOI_MARKERS[] = { 0xFF, 0xD8 };

    static constexpr int BUTTON_DEBOUNCE_TIME_MS = 200;
    static constexpr int QUICK_PRESS_MIN_MS = 150;
    static constexpr int QUICK_PRESS_MAX_MS = 450;
    static constexpr int JPEG_SOI_MARKERS_MAX_POSITION = 32;

    std::atomic<bool> running{true};
    uint32_t frameId = 0;
    std::vector<uint8_t> frameBuffer;
    std::mutex frameMutex;
    std::vector<uint8_t> snapshotBuffer;
    std::mutex snapshotMutex;
    std::atomic<bool> snapshotNextFrame{false};
    std::mutex buttonMutex;
    
    std::chrono::steady_clock::time_point buttonPressStart = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point buttonLastSeen = std::chrono::steady_clock::now();
    bool buttonIsDepressed = false;
};



