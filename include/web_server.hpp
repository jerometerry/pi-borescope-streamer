#pragma once

#include "client_state.hpp"
#include <poll.h>
#include <atomic>
#include <thread>
#include <memory>
#include <mutex>
#include <vector>
#include <cstdint>

/** 
 * @brief Class representing a web server
 */
class WebServer {
public:
    /** 
     * @brief Construct a new web server instance
     * @param port The port to listen on
     * @param running A flag indicating whether the server is running
     * @param latestFrameId The ID of the latest frame
     * @param frameBuffer The buffer containing the latest frame
     * @param frameMutex The mutex for protecting the frame buffer
     * @param snapshotBuffer The buffer containing the latest snapshot
     * @param snapshotMutex The mutex for protecting the snapshot buffer
     */
    explicit WebServer(const int port,
                       const std::atomic<bool>& running,               
                       const uint32_t& latestFrameId,                
                       const std::vector<uint8_t>& frameBuffer,
                       std::mutex& frameMutex,
                       const std::vector<uint8_t>& snapshotBuffer,
                       std::mutex& snapshotMutex);

    /** 
     * @brief Destroy the web server instance
     */
    ~WebServer();

    /** 
     * @brief Copy constructor for the web server instance
     */
    WebServer(const WebServer&) = delete;

    /** 
     * @brief Assignment operator for the web server instance
     * @return A reference to the assigned web server instance
     */
    WebServer& operator=(const WebServer&) = delete;

    /** 
     * @brief Initialize the web server
     * @return True if initialization was successful, false otherwise
     */
    bool initialize();

    /** 
     * @brief Start the web server
     */
    void start();

private:
    /** 
     * @brief The main event loop for the web server
     */
    void eventLoop();
    /** 
     * @brief Handle incoming connections
     */
    void handleAccept();
    /** 
     * @brief Handle read events
     */
    void handleRead(int fileDescriptor);
    /** 
     * @brief Handle write events
     */
    void handleWrite(int fileDescriptor);
    /** 
     * @brief Broadcast the latest frame to all connected clients
     */
    void broadcastLatestFrame();

    /** 
     * @brief Close a connection
     * @param fileDescriptor The file descriptor of the connection to close
     */
    void closeConnection(int fileDescriptor);

    /** 
     * @brief Set a file descriptor to non-blocking mode
     * @param fileDescriptor The file descriptor to set
     * @return True if successful, false otherwise
     */
    static bool setNonBlocking(int fileDescriptor);

    /** 
     * @brief Process a client request
     * @param client The client state
     */
    void processClientRequest(ClientState& client);

    /** 
     * @brief Queue data for a client
     * @param client The client state
     * @param data The data to queue
     * @param size The size of the data
     */
    void queueData(ClientState& client, const uint8_t* data, size_t size);

    /** 
     * @brief The maximum number of concurrent clients
     */
    static constexpr size_t MAX_CLIENTS = 8;
    /** 
     * @brief The initial capacity of the poll file descriptors vector
     */
    static constexpr size_t INITIAL_POLL_CAPACITY = 16;
    /** 
     * @brief The size of the stack buffer for handling HTTP headers
     */
    static constexpr size_t STACK_BUF_SIZE = 128;

    /** 
     * @brief The prefix for MJPEG chunks
     */
    static constexpr std::string_view MJPEG_CHUNK_PREFIX = "--mjpegstream\r\nContent-Type: image/jpeg\r\nContent-Length: ";
    
    /** 
     * @brief The suffix for MJPEG chunks
     */
    static constexpr std::string_view MJPEG_CHUNK_SUFFIX = "\r\n\r\n";

    /** 
     * @brief The delimiter for HTTP headers
     */
    static constexpr std::string_view HEADER_DELIMITER  = "\r\n\r\n";

    /** 
     * @brief The route for the web page
     */
    static constexpr std::string_view ROUTE_WEB          = "GET /web";

    /** 
     * @brief The route for the snapshot endpoint
     */
    static constexpr std::string_view ROUTE_SNAPSHOT     = "GET /snapshot";

    /** 
     * @brief The route for the favicon endpoint
     */
    static constexpr std::string_view ROUTE_FAVICON      = "GET /favicon.ico";

    /** 
     * @brief The format string for HTTP 200 OK responses with HTML content
     */
    static constexpr std::string_view HTTP_OK_HTML_FMT   = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: {}\r\nConnection: close\r\n\r\n";

    /** 
     * @brief The format string for HTTP 200 OK responses with JPEG content
     */
    static constexpr std::string_view HTTP_OK_JPEG_FMT   = "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: {}\r\nConnection: close\r\n\r\n";

    /** 
     * @brief The format string for HTTP 200 OK responses with MJPEG content
     */
    static constexpr std::string_view HTTP_OK_MJPEG      = "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: no-cache, private\r\nPragma: no-cache\r\nContent-Type: multipart/x-mixed-replace; boundary=mjpegstream\r\n\r\n";

    /** 
     * @brief The format string for HTTP 404 Not Found responses
     */
    static constexpr std::string_view HTTP_NOT_FOUND     = "HTTP/1.1 404 Not Found\r\nCache-Control: no-cache\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";

    /** 
     * @brief The format string for favicon not found responses
     */
    static constexpr std::string_view FAVICON_NOT_FOUND  = "HTTP/1.1 404 Not Found\r\nCache-Control: public, max-age=31536000\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";

    /** 
     * @brief The format string for MJPEG frame headers
     */
    static constexpr std::string_view MJPEG_FRAME_FMT    = "--mjpegstream\r\nContent-Type: image/jpeg\r\nContent-Length: {}\r\n\r\n";

    /** 
     * @brief The format string for MJPEG frame footers
     */
    static constexpr std::string_view MJPEG_FOOTER       = "\r\n";

    /** 
     * @brief The error message for socket allocation failures
     */
    static constexpr std::string_view ERR_SOCKET         = "[Web Server Error] Failed to allocate base server socket.\n";

    /** 
     * @brief The error message for binding failures
     */
    static constexpr std::string_view ERR_BIND           = "[Web Server Error] Binding network interface to port {} failed.\n";

    /** 
     * @brief The error message for listen failures
     */
    static constexpr std::string_view ERR_LISTEN         = "[Web Server Error] Backlog listener setup failed.\n";

    /** 
     * @brief The error message for non-blocking failures
     */
    static constexpr std::string_view ERR_NONBLOCK       = "[Web Server Error] Failed to set non-blocking on listener.\n";

    /** 
     * @brief The file descriptor for the listening socket
     */
    int listenFileDescriptor = -1;

    /** 
     * @brief A stack buffer for storing HTTP headers
     */
    char headerStackBuf[STACK_BUF_SIZE]{};

    /** 
     * @brief A collection of client states
     */
    std::unique_ptr<std::array<ClientState, MAX_CLIENTS>> clients;

    /** 
     * @brief A mutex for protecting the client states
     */
    std::mutex clientsMutex;

    /** 
     * @brief A vector of poll file descriptors
     */
    std::vector<struct pollfd> pollFileDescriptors;

    /** 
     * @brief The worker thread for handling client connections
     */
    std::thread workerThread;

    /** 
     * @brief The port on which the server will listen
     */
    const int port;

    /** 
     * @brief A reference to the running flag
     */
    const std::atomic<bool>& running;

    /** 
     * @brief A reference to the latest frame ID
     */
    const uint32_t& latestFrameId;

    /** 
     * @brief A reference to the frame buffer
     */
    const std::vector<uint8_t>& frameBuffer;

    /** 
     * @brief A reference to the frame mutex
     */
    std::mutex& frameMutex;

    /** 
     * @brief A reference to the snapshot buffer
     */
    const std::vector<uint8_t>& snapshotBuffer;

    /** 
     * @brief A reference to the snapshot mutex
     */
    std::mutex& snapshotMutex;
};