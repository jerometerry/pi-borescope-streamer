#pragma once

#include <poll.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>
#include "client_connection.hpp"
#include "server_constants.hpp"

class SharedFramePipeline;

class WebServer {
public:
    explicit WebServer(const int port,
                       const std::atomic<bool>& running,               
                       SharedFramePipeline& pipeline);
    ~WebServer();

    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;

    bool initialize();
    void start();

private:
    void eventLoop();
    void handleAccept();
    void handleRead(int fileDescriptor);
    void handleWrite(int fileDescriptor);
    void broadcastLatestFrame();
    void closeConnection(int fileDescriptor);
    
    [[nodiscard]] static bool setNonBlocking(int fileDescriptor);
    void processClientRequest(ClientConnection& client);

    static constexpr std::string_view MJPEG_CHUNK_PREFIX = "--mjpegstream\r\nContent-Type: image/jpeg\r\nContent-Length: ";
    static constexpr std::string_view MJPEG_CHUNK_SUFFIX = "\r\n\r\n";

    static constexpr std::string_view HEADER_DELIMITER  = "\r\n\r\n";
    static constexpr std::string_view ROUTE_WEB          = "GET /web";
    static constexpr std::string_view ROUTE_SNAPSHOT     = "GET /snapshot";
    static constexpr std::string_view ROUTE_FAVICON      = "GET /favicon.ico";

    static constexpr std::string_view MJPEG_FOOTER = "\r\n";

    static constexpr std::string_view ERR_SOCKET   = "[Web Server Error] Failed to allocate base server socket.\n";
    static constexpr std::string_view ERR_BIND     = "[Web Server Error] Binding network interface to port failed.\n";
    static constexpr std::string_view ERR_LISTEN   = "[Web Server Error] Backlog listener setup failed.\n";
    static constexpr std::string_view ERR_NONBLOCK = "[Web Server Error] Failed to set non-blocking on listener.\n";

    int listenFileDescriptor = -1;

    std::unique_ptr<std::array<ClientConnection, ServerConstants::MAX_CLIENTS>> clients;
    std::mutex clientsMutex;
    
    std::vector<struct pollfd> pollFileDescriptors;
    std::thread workerThread;

    const int port;
    const std::atomic<bool>& running;
    SharedFramePipeline& pipeline;
    uint32_t localLatestFrameId{0};
};
