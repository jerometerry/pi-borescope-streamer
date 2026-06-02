#pragma once

#include <poll.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>
#include "server_constants.hpp"

class ClientConnection;
class SharedFramePipeline;

/**
 * @brief The server that streams the USB camera video to your web browser or video player.
 * @details MjpegServer is the bridge between the physical USB camera and your network. 
 * It takes the raw video pictures coming from the hardware and packages them into an MJPEG 
 * stream that any standard web browser (like Chrome or Safari) or video player (like VLC) 
 * can easily display.
 * 
 * It is built to be extremely lightweight and fast. It allows multiple devices (like your 
 * phone and your laptop) to watch the stream at the exact same time without forcing the 
 * physical camera to work any harder. It runs smoothly in the background without eating up 
 * your Raspberry Pi's memory, even if it runs for days.
 */
class MjpegServer {
public:

    /**
     * @brief Set up the streaming server.
     * @details Prepares the server to listen on the given network port. It won't actually 
     * start accepting connections or streaming video until `initialize()` and `start()` are called.
     * @param port The network port to broadcast on (e.g., 8080).
     * @param running A safety switch to gracefully shut down the server when the program exits.
     * @param pipeline The shared pipeline where the fresh video frames from the camera are waiting.
     */
    explicit MjpegServer(const int port,
                         const std::atomic<bool>& running,               
                         SharedFramePipeline& pipeline);

    /**
     * @brief Shut down the server and clean up.
     * @details Safely stops the background video broadcast, kicks out any connected viewers, 
     * and frees up the network port.
     */
    ~MjpegServer();

    /**
     * @brief Delete the copy constructor to prevent accidentally duplicating the server.
     */
    MjpegServer(const MjpegServer&) = delete;

    /**
     * @brief Delete the assignment operator to prevent accidentally duplicating the server.
     */
    MjpegServer& operator=(const MjpegServer&) = delete;

    /**
     * @brief Bind the server to the network port.
     * @details Claims the requested port on the Raspberry Pi so it can start listening 
     * for incoming viewers. It also applies settings to prevent the server from crashing 
     * if a viewer abruptly disconnects (like closing their phone screen).
     * @return true if the port was successfully claimed, false if it is already in use.
     */
    bool initialize();

    /**
     * @brief Fire up the background engine to start broadcasting.
     * @details Launches a dedicated background process that continuously watches for new 
     * viewers and hands out video frames.
     */
    void start();

private:
    /**
     * @brief The main background loop that keeps the video flowing.
     * @details This is the beating heart of the server. Roughly 30 times a second, it wakes up to:
     * 1. Give the newest video frame to everyone currently watching.
     * 2. Let new viewers connect.
     * 3. Clean up the connections of anyone who left.
     * It does all of this incredibly fast without pausing or stuttering.
     */
    void eventLoop();

    /**
     * @brief Send the newest picture to all viewers.
     * @details Checks if the camera has captured a new picture since the last check. 
     * If it has, it wraps the picture in standard web formatting and drops a copy into 
     * the "outbox" of every connected viewer.
     */
    void broadcastLatestFrame();

    /**
     * @brief Welcome a new viewer to the stream.
     * @details When someone types the Raspberry Pi's IP address into their browser, 
     * this accepts their connection and gives them a dedicated viewing slot, as long 
     * as the server isn't already full.
     */
    void handleAccept();

    /**
     * @brief Read what the viewer is asking for.
     * @param fileDescriptor The specific network connection to read from.
     * @details Reads the incoming web request to figure out if the viewer wants the 
     * main dashboard page, the live video stream, or just a single snapshot picture.
     */
    void handleRead(int fileDescriptor);

    /**
     * @brief Push the waiting video data out over the Wi-Fi/Ethernet.
     * @param fileDescriptor The specific network connection to send data to.
     * @details Empties the viewer's "outbox" and actually pushes the video bytes over 
     * the network. If a viewer has a terrible Wi-Fi connection and the server can't push 
     * data fast enough, it will gently kick them out so they don't lag the whole system.
     */
    void handleWrite(int fileDescriptor);

    /**
     * @brief Forcibly kick a viewer and close their connection.
     * @param fileDescriptor The network connection to close.
     * @details Safely finishes sending any final bytes, hangs up the connection, and 
     * frees up their viewing slot for someone else.
     */
    void closeConnection(int fileDescriptor);
    
    /**
     * @brief Tell the network connection not to freeze the server.
     * @param fileDescriptor The connection to modify.
     * @return true if successful, false otherwise.
     */
    [[nodiscard]] static bool setNonBlocking(int fileDescriptor);

    /**
     * @brief Route the viewer to the right place.
     * @param client The viewer's connection data.
     * @details Acts like a traffic cop. If they ask for `/stream`, they get the video. 
     * If they ask for `/`, they get the HTML control page.
     */
    void processClientRequest(ClientConnection& client) const;

    /**
     * @brief The main doorway where new viewers knock to get in.
     */
    int listenFileDescriptor = -1;

    /**
     * @brief The list of all available viewing slots.
     * @details Created once when the server starts. Limiting this to a fixed maximum 
     * number keeps the Raspberry Pi from running out of memory.
     */
    std::unique_ptr<std::array<ClientConnection, ServerConstants::MAX_CLIENTS>> clients;

    /**
     * @brief A lock to safely manage people joining and leaving.
     */
    std::mutex clientsMutex;

    /**
     * @brief The operating system checklist of active network connections.
     */
    std::array<struct pollfd, ServerConstants::MAX_CLIENTS + 1> pollFds{};

    /**
     * @brief The background worker that handles all the streaming.
     */
    std::thread workerThread;

    /**
     * @brief The port number the server broadcasts on.
     */
    const int port;

    /**
     * @brief The master switch that keeps the server running.
     */
    const std::atomic<bool>& running;

    /**
     * @brief The pipeline where the server picks up the fresh video.
     */
    SharedFramePipeline& pipeline;

    /**
     * @brief Keeps track of the last picture sent, so we don't send duplicates.
     */
    uint64_t lastBroadcastedFrameId{0};
};