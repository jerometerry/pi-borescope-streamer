#pragma once

#include "client_state.hpp"
#include <poll.h>
#include <atomic>
#include <thread>
#include <memory>
#include <mutex>
#include <vector>
#include <cstdint>

class WebServer {
public:
    explicit WebServer(const int port,
                       const std::atomic<bool>& running,               
                       const uint32_t& latestFrameId,                
                       const std::vector<uint8_t>& frameBuffer,
                       std::mutex& frameMutex,
                       const std::vector<uint8_t>& snapshotBuffer,
                       std::mutex& snapshotMutex);
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
    bool setNonBlocking(int fileDescriptor);
    void processClientRequest(ClientState& client);
    void queueData(ClientState& client, const uint8_t* data, size_t size);

    static constexpr size_t MAX_CLIENTS = 8;
    static constexpr size_t INITIAL_POLL_CAPACITY = 16;
    static constexpr size_t STACK_BUF_SIZE = 128;

    static constexpr std::string_view MJPEG_CHUNK_PREFIX = "--mjpegstream\r\nContent-Type: image/jpeg\r\nContent-Length: ";
    static constexpr std::string_view MJPEG_CHUNK_SUFFIX = "\r\n\r\n";

    static constexpr std::string_view HEADER_DELIMITER  = "\r\n\r\n";
    static constexpr std::string_view ROUTE_WEB          = "GET /web";
    static constexpr std::string_view ROUTE_SNAPSHOT     = "GET /snapshot";
    static constexpr std::string_view ROUTE_FAVICON      = "GET /favicon.ico";

    static constexpr std::string_view HTTP_OK_HTML_FMT   = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: {}\r\nConnection: close\r\n\r\n";
    static constexpr std::string_view HTTP_OK_JPEG_FMT   = "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: {}\r\nConnection: close\r\n\r\n";
    static constexpr std::string_view HTTP_OK_MJPEG      = "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: no-cache, private\r\nPragma: no-cache\r\nContent-Type: multipart/x-mixed-replace; boundary=mjpegstream\r\n\r\n";
    static constexpr std::string_view HTTP_NOT_FOUND     = "HTTP/1.1 404 Not Found\r\nCache-Control: no-cache\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    static constexpr std::string_view FAVICON_NOT_FOUND  = "HTTP/1.1 404 Not Found\r\nCache-Control: public, max-age=31536000\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";

    static constexpr std::string_view MJPEG_FRAME_FMT    = "--mjpegstream\r\nContent-Type: image/jpeg\r\nContent-Length: {}\r\n\r\n";
    static constexpr std::string_view MJPEG_FOOTER       = "\r\n";

    static constexpr std::string_view ERR_SOCKET         = "[Web Server Error] Failed to allocate base server socket.\n";
    static constexpr std::string_view ERR_BIND           = "[Web Server Error] Binding network interface to port {} failed.\n";
    static constexpr std::string_view ERR_LISTEN         = "[Web Server Error] Backlog listener setup failed.\n";
    static constexpr std::string_view ERR_NONBLOCK       = "[Web Server Error] Failed to set non-blocking on listener.\n";

    int listenFileDescriptor = -1;
    char headerStackBuf[STACK_BUF_SIZE]{};

    std::unique_ptr<std::array<ClientState, MAX_CLIENTS>> clients;
    std::mutex clientsMutex;
    std::vector<struct pollfd> pollFileDescriptors;
    std::thread workerThread;

    const int port;
    const std::atomic<bool>& running;
    const uint32_t& latestFrameId;
    const std::vector<uint8_t>& frameBuffer;
    std::mutex& frameMutex;
    const std::vector<uint8_t>& snapshotBuffer;
    std::mutex& snapshotMutex;
};