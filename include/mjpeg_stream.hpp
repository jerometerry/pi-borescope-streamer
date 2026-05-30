#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <mutex>
#include <vector>
#include "server_time.hpp"
struct DeviceInfo;

/** 
 * @brief Class representing an MJPEG video stream
 */
class MjpegStream {
public:
    /** 
     * @brief Construct a new MJPEG stream
     * @param serverTime The server time instance
     */
    explicit MjpegStream(const ServerTime& serverTime);

    /** 
     * @brief Destroy the MJPEG stream
     */
    ~MjpegStream();

    /** 
     * @brief Delete the copy constructor
     */
    MjpegStream(const MjpegStream&) = delete;

    /** 
     * @brief Delete the assignment operator
     */
    MjpegStream& operator=(const MjpegStream&) = delete;
    /** 
     * @brief Delete the move constructor
     */
    MjpegStream(MjpegStream&&) = delete;

    /** 
     * @brief Delete the move assignment operator
     */
    MjpegStream& operator=(MjpegStream&&) = delete;

    /** 
     * @brief Run the MJPEG stream
     * @param port The port to listen on
     * @param target The target device
     * @return The result of the operation
     */
    int run(int port, const DeviceInfo& target);

    /** 
     * @brief Stop the MJPEG stream
     */
    void stop();

private:
    friend class MjpegStreamTest;

    /** 
     * @brief Broadcast the latest frame
     * @param frame The frame to broadcast
     */
    void broadcastFrame(const std::vector<uint8_t>& frame);

    /** 
     * @brief Handle hardware button callback
     */
    void hardwareButtonCallback();

    /**
     * @brief
     */
    void checkForButtonQuickPress();

    /** 
     * @brief Start the video feed
     * @param target The target device
     */
    void startVideoFeed(const DeviceInfo& target);

    /** 
     * @brief Whether the stream is running
     */
    std::atomic<bool> running{true};

    /** 
     * @brief The frame ID
     */
    uint32_t frameId = 0;

    /** 
     * @brief The frame buffer
     */
    std::vector<uint8_t> frameBuffer;

    /** 
     * @brief The frame mutex
     */
    std::mutex frameMutex;

    /** 
     * @brief The snapshot buffer
     */
    std::vector<uint8_t> snapshotBuffer;

    /** 
     * @brief The snapshot mutex
     */
    std::mutex snapshotMutex;

    /** 
     * @brief Whether to take a snapshot of the next frame
     */
    std::atomic<bool> snapshotNextFrame{false};

    /** 
     * @brief The button mutex
     */
    std::mutex buttonMutex;

    /** 
     * @brief The last time the button was seen
     */
    std::chrono::steady_clock::time_point buttonPressStart = std::chrono::steady_clock::now();

    /** 
     * @brief The last time the button was seen
     */
    std::chrono::steady_clock::time_point buttonLastSeen = std::chrono::steady_clock::now();

    /**
     * 
     */
    bool buttonIsDepressed = false;

    /** 
     * @brief The server time instance
     */
    const ServerTime serverTime;
};
