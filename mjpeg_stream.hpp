#pragma once

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

class MjpegStream {

public:
    MjpegStream();
    ~MjpegStream();

    void broadcastFrame(const std::vector<uint8_t>& frame);
    void hardwareButtonCallback();
    void checkForButtonQuickPress();
    void startVideoFeed();

    int run(int port);
    void stop();

private:
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



