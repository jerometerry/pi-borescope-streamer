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
 * @brief A web server for handling client connections and broadcasting frames
 */
class WebServer {
public:

    /** 
     * @brief Construct a new web server
     * @param port The port to listen on
     * @param running A flag indicating if the server should be running
     * @param pipeline The shared frame pipeline
     */
    explicit WebServer(const int port,
                       const std::atomic<bool>& running,               
                       SharedFramePipeline& pipeline);

   
    /** 
     * @brief Destruct the web server
     */
    ~WebServer();

    /** 
     * @brief Delete the copy constructor
     */
    WebServer(const WebServer&) = delete;

    /** 
     * @brief Delete the assignment operator
     */
    WebServer& operator=(const WebServer&) = delete;

    /** 
     * @brief Initialize the web server
     * @return true if initialization was successful, false otherwise
     */
    bool initialize();

    /** 
     * @brief Start the web server
     */
    void start();

private:
    /** 
     * @brief The prefix for MJPEG chunks
     */
    static constexpr std::string_view MJPEG_CHUNK_PREFIX = 
        "--mjpegstream\r\nContent-Type: image/jpeg\r\nContent-Length: ";

    /** 
     * @brief The suffix for MJPEG chunks
     */
    static constexpr std::string_view MJPEG_CHUNK_SUFFIX = "\r\n\r\n";

    /** 
     * @brief The delimiter for HTTP headers
     */
    static constexpr std::string_view HEADER_DELIMITER = "\r\n\r\n";

    /** 
     * @brief The route for the web interface
     */
    static constexpr std::string_view ROUTE_WEB = "GET /web";

    /** 
     * @brief The route for taking a snapshot
     */
    static constexpr std::string_view ROUTE_SNAPSHOT = "GET /snapshot";

    /** 
     * @brief The route for the favicon
     */
    static constexpr std::string_view ROUTE_FAVICON = "GET /favicon.ico";

    /** 
     * @brief The HTTP response for a 404 Not Found
     */
    static constexpr std::string_view MJPEG_FOOTER = "\r\n";

    /** 
     * @brief The error message for socket allocation failure
     */
    static constexpr std::string_view ERR_SOCKET = "[Web Server Error] Failed to allocate base server socket.\n";

    /** 
     * @brief The error message for binding failure
     */
    static constexpr std::string_view ERR_BIND = "[Web Server Error] Binding network interface to port failed.\n";

    /** 
     * @brief The error message for listen setup failure
     */
    static constexpr std::string_view ERR_LISTEN = "[Web Server Error] Backlog listener setup failed.\n";

    /** 
     * @brief The error message for non-blocking setup failure
     */
    static constexpr std::string_view ERR_NONBLOCK = "[Web Server Error] Failed to set non-blocking on listener.\n";

    /** 
     * @brief The error message for accept failure
     */
    void eventLoop();

    /** 
     * @brief Handle a new client connection
     */
    void handleAccept();

    /** 
     * @brief Handle data read from a client
     * @param fileDescriptor The file descriptor for the client
     */
    void handleRead(int fileDescriptor);

    /** 
     * @brief Handle data write to a client
     * @param fileDescriptor The file descriptor for the client
     */
    void handleWrite(int fileDescriptor);

    /** 
     * @brief Broadcast the latest frame to all connected clients
     */
    void broadcastLatestFrame();

    /** 
     * @brief Close a client connection
     * @param fileDescriptor The file descriptor for the client
     */
    void closeConnection(int fileDescriptor);
    
    /** 
     * @brief Set the non-blocking mode for a file descriptor
     * @param fileDescriptor The file descriptor to set
     * @return true if successful, false otherwise
     */
    [[nodiscard]] static bool setNonBlocking(int fileDescriptor);

    /** 
     * @brief Process a client request
     * @param client The client connection
     */
    void processClientRequest(ClientConnection& client) const;

    /** 
     * @brief The file descriptor for the listening socket
     */
    int listenFileDescriptor = -1;

    /** 
     * @brief A list of all connected clients
     */
    std::unique_ptr<std::array<ClientConnection, ServerConstants::MAX_CLIENTS>> clients;

    /** 
     * @brief A mutex to protect the clients list
     */
    std::mutex clientsMutex;

    /** 
     * @brief An array of poll file descriptors
     */
    std::array<struct pollfd, ServerConstants::MAX_CLIENTS + 1> pollFds{};

    /** 
     * @brief A thread for handling client events
     */
    std::thread workerThread;

    /** 
     * @brief The port on which the server listens
     */
    const int port;

    /** 
     * @brief A flag indicating whether the server is running
     */
    const std::atomic<bool>& running;

    /** 
     * @brief A reference to the shared frame pipeline
     */
    SharedFramePipeline& pipeline;

    /** 
     * @brief The ID of the latest frame available locally
     */
    uint32_t localLatestFrameId{0};
};
