#include <arpa/inet.h> 
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <format>
#include <iostream>
#include <string>
#include "client_state.hpp"
#include "index_html.hpp"
#include "shared_frame_pipeline.hpp"
#include "web_server.hpp"

// Cleanly aligned to strictly match Header declaration layout rules (C++ Core Guidelines Rule C.47)
WebServer::WebServer(const int port,
                     const std::atomic<bool>& running,
                     SharedFramePipeline& pipeline)
    : clients(std::make_unique<std::array<ClientState, MAX_CLIENTS>>()),
      port(port),
      running(running),
      pipeline(pipeline) {
    std::cout << "[Network Core] WebServer subsystem successfully provisioned on port " << port << ".\n";
}

WebServer::~WebServer() {
    std::cout << "[Network Core] Initiating application engine teardown procedure...\n";

    // 1. Force break the background poll loop by dismantling the listener first
    if (listenFileDescriptor != -1) {
        close(listenFileDescriptor);
        listenFileDescriptor = -1;
    }

    // 2. Safely await the background event loop thread closure
    if (workerThread.joinable()) {
        workerThread.join();
    }

    // 3. Cleanly close all lingering client connections to avoid OS socket descriptor leaks
    std::scoped_lock<std::mutex> lock(clientsMutex);
    size_t activeConnectionsClosed = 0;
    
    for (auto& client : *clients) {
        if (client.isActive && client.fileDescriptor != -1) {
            close(client.fileDescriptor);
            client.isActive = false;
            client.outbox.clear(); // Reclaims systemic heap allocations immediately
            activeConnectionsClosed++;
        }
    }
    
    if (activeConnectionsClosed > 0) {
        std::cout << "[Network Core] Evicted " << activeConnectionsClosed << " active sockets during lifecycle teardown.\n";
    }
}

bool WebServer::initialize() {
    // 1. Thread-safe POSIX signal configuration to ignore SIGPIPE.
    // Replaces raw signal() to comply with strict C++ static analysis guidelines.
    struct sigaction signalAction{};
    signalAction.sa_handler = SIG_IGN;
    sigemptyset(&signalAction.sa_mask);
    signalAction.sa_flags = 0;
    sigaction(SIGPIPE, &signalAction, nullptr);

    // 2. Open standard internet streaming socket
    listenFileDescriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFileDescriptor == -1) {
        std::cerr << ERR_SOCKET;
        return false;
    }

    // 3. Configure Socket Reuse Parameters
    int opt = 1;
    setsockopt(listenFileDescriptor, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // FIXED: Apply SO_REUSEPORT to allow instant service restarts on Linux 
    // even if active live MJPEG web clients are midway through a stream.
#if defined(__linux__) || defined(__APPLE__)
    setsockopt(listenFileDescriptor, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    // 4. Bind Network Parameters
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(listenFileDescriptor, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "Error binding to port: " << port << '\n';
        close(listenFileDescriptor);
        listenFileDescriptor = -1;
        return false;
    }

    // 5. Establish Listener Backlog Queue
    if (listen(listenFileDescriptor, 10) < 0) {
        std::cerr << ERR_LISTEN;
        close(listenFileDescriptor);
        listenFileDescriptor = -1;
        return false;
    }

    // Configure the listener handle as non-blocking
    if (!setNonBlocking(listenFileDescriptor)) {
        std::cerr << ERR_NONBLOCK;
        close(listenFileDescriptor);
        listenFileDescriptor = -1;
        return false;
    }

    return true;
}


void WebServer::start() {
    // 1. Structural Execution Guard: Prevent double-activation thread crashes
    if (workerThread.joinable()) {
        std::cerr << "[Network Core Warning] WebServer::start() called on an already active engine instance.\n";
        return; 
    }

    // 2. Pre-allocate vector capacity safely on the main thread 
    // before any background worker concurrency begins.
    pollFileDescriptors.clear();
    pollFileDescriptors.reserve(INITIAL_POLL_CAPACITY);

    // 3. Spawn background event loop worker thread safely
    std::cout << "[Network Core] Launching asynchronous engine worker thread...\n";
    workerThread = std::thread(&WebServer::eventLoop, this);
}


void WebServer::eventLoop() {
    while (running) {
        // 1. Process and broadcast the latest video frames to active streams
        broadcastLatestFrame();

        // 2. Tightly scoped critical section to construct our socket lookup list
        {
            std::scoped_lock<std::mutex> lock(clientsMutex);

            pollFileDescriptors.clear();
            pollFileDescriptors.push_back({listenFileDescriptor, POLLIN, 0});
            
            for (const auto& client : *clients) {
                if (!client.isActive) {
                    continue;
                }
                short events = POLLIN;
                if (client.outboxLen > 0) {
                    events |= POLLOUT; // Monitor for write readiness only if data is waiting
                }
                pollFileDescriptors.push_back({client.fileDescriptor, events, 0});
            }
        }

        // 3. Changed timeout from 10ms to a passive 30ms window.
        // This lines up with your 30 FPS camera interval, dropping idle CPU usage to near 0%.
        int ret = poll(pollFileDescriptors.data(), pollFileDescriptors.size(), 30);
        if (ret == -1) {
            if (errno == EINTR) { 
                continue; // System signal interrupted call; resume loop safely
            }
            break;
        }

        // 4. Thread-safe sequential execution loop processing network events
        for (const auto& pfd : pollFileDescriptors) {
            if (pfd.revents == 0) { 
                continue; 
            }

            if (pfd.fd == listenFileDescriptor) {
                if (pfd.revents & POLLIN) { 
                    handleAccept(); 
                }
            } else {
                // FIXED: Mutually exclusive event routing.
                // If a hard socket error occurs, tear down the connection and immediately advance 
                // to the next client. This prevents running read/write logic on a closed slot.
                if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    closeConnection(pfd.fd);
                } 
                else {
                    if (pfd.revents & POLLIN) { 
                        handleRead(pfd.fd); 
                    }
                    // Only attempt a write flush if the client connection survived the handleRead pass
                    if (pfd.revents & POLLOUT) { 
                        handleWrite(pfd.fd); 
                    }
                }
            }
        }
    }
}


bool WebServer::setNonBlocking(int fileDescriptor) {
    if (fileDescriptor < 0) {
        return false;
    }

    // Explicitly use a regular int, but ensure we match return and error boundaries perfectly
    const int currentFlags = fcntl(fileDescriptor, F_GETFL, 0);
    if (currentFlags == -1) { 
        std::cerr << "[Network Utilities] Failed to retrieve socket flags on FD " << fileDescriptor << ".\n";
        return false; 
    }

    // Mask out the O_NONBLOCK flag safely using standard bitwise operators
    const int targetFlags = currentFlags | O_NONBLOCK;
    
    // fcntl returns 0 on success, -1 on failure
    if (fcntl(fileDescriptor, F_SETFL, targetFlags) == -1) {
        std::cerr << "[Network Utilities] Failed to apply O_NONBLOCK flag on FD " << fileDescriptor << ".\n";
        return false;
    }

    return true;
}


void WebServer::handleAccept() {
    while (true) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        
        // 1. Accept incoming socket connection from kernel listener queue
        int fileDescriptor = accept(
            listenFileDescriptor, 
            reinterpret_cast<struct sockaddr*>(&clientAddr), 
            &clientLen
        );
        
        if (fileDescriptor == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) { 
                break; // Queue is fully empty; yield thread back to poll()
            }
            if (errno == EINTR) {
                continue; // Interrupted by a system signal call; retry immediately
            }
            break;
        }

        // Configure connection as non-blocking right away
        if (!setNonBlocking(fileDescriptor)) {
            close(fileDescriptor);
            continue;
        }

        // 2. Tightly look up structural slots using the global shared mutex
        std::scoped_lock<std::mutex> lock(clientsMutex);

        auto it = std::find_if(clients->begin(), clients->end(), [](const ClientState& c) {
            return !c.isActive;
        });

        if (it != clients->end()) {
            // Found open slot: Bind file descriptor and initialize client connection state
            it->reset(fileDescriptor);
        } else {
            // FIXED: Server is completely full. Reject cleanly with an explicit HTTP 503 error header
            // rather than dropping the socket silently.
            std::string_view rejectionHeader = 
                "HTTP/1.1 503 Service Unavailable\r\n"
                "Content-Type: text/plain\r\n"
                "Connection: close\r\n\r\n"
                "Server Capacity Reached.\n";
                
            // Send rejection on the wire immediately using MSG_NOSIGNAL to prevent pipeline crashes
            send(fileDescriptor, rejectionHeader.data(), rejectionHeader.size(), MSG_NOSIGNAL);
            
            // Dismantle kernel resource handle cleanly
            close(fileDescriptor);
        }
    }
}


void WebServer::handleRead(int fileDescriptor) {
    // 1. Allocate a transient, thread-local staging array on the stack.
    // This allows us to perform non-blocking kernel reads without holding ANY global locks.
    std::array<char, ClientState::READ_BUFFER_SIZE> temporaryBuffer{};
    ssize_t bytesRead = 0;

    // 2. Perform the kernel read completely isolated from your streaming data structures
    while (true) {
        bytesRead = recv(fileDescriptor, temporaryBuffer.data(), temporaryBuffer.size(), 0);
        if (bytesRead == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return; // Yield cleanly; the socket's receive queue is currently empty
            }
            if (errno == EINTR) {
                continue; // System call was interrupted by a signal; retry immediately
            }
            closeConnection(fileDescriptor);
            return;
        }
        break;
    }

    if (bytesRead == 0) {
        closeConnection(fileDescriptor); // Client initiated a clean connection shutdown
        return;
    }

    // 3. Tightly lock the critical section ONLY when appending data to the shared state array
    std::scoped_lock<std::mutex> lock(clientsMutex);

    auto it = std::find_if(clients->begin(), clients->end(), [fileDescriptor](const ClientState& c) {
        return c.isActive && c.fileDescriptor == fileDescriptor;
    });

    if (it == clients->end()) { 
        return; // The connection was severed by another thread during our socket read
    }

    auto& client = *it;

    // Verify we have enough remaining capacity in our vector buffer
    if (client.readBufferLen + static_cast<size_t>(bytesRead) > client.readBuffer.size()) {
        std::cerr << "[Network Core] Input buffer overflow on FD " << fileDescriptor << ". Terminating.\n";
        // Let the lock drop, then clean up. (Or flag inactive for eventLoop to clean up)
        client.isActive = false;
        return;
    }

    // Safely copy the staging data into the client's localized memory structure
    std::memcpy(client.readBuffer.data() + client.readBufferLen, temporaryBuffer.data(), bytesRead);
    client.readBufferLen += bytesRead;

    // 4. Parse the data within the safe boundary context of our structural lock
    std::string_view incoming(client.readBuffer.data(), client.readBufferLen);
    if (incoming.find(HEADER_DELIMITER) != std::string_view::npos) {
        processClientRequest(client);
    }
}

void WebServer::processClientRequest(ClientState& client) {
    std::string_view request(client.readBuffer.data(), client.readBufferLen);

    // 1. Web Dashboard Route Matching
    std::string targetWebPath = std::format("GET {}", ROUTE_WEB);
    size_t webPos = request.find(targetWebPath);
    if (webPos != std::string_view::npos && 
       (request[webPos + targetWebPath.size()] == ' ' || request[webPos + targetWebPath.size()] == '?')) {
        
        client.closeAfterWrite = true;
        auto htmlPageContent = Resources::index_html;

        auto [it, size] = std::format_to_n(
            headerStackBuf, STACK_BUF_SIZE, HTTP_OK_HTML_FMT, htmlPageContent.size()
        );

        client.queueData(reinterpret_cast<const uint8_t*>(headerStackBuf), size);
        client.queueData(reinterpret_cast<const uint8_t*>(htmlPageContent.data()), htmlPageContent.size());
    } 
    // 2. FIXED: Snapshot Route Parameter Validation Matching Boundary
    else if (size_t snapPos = request.find(std::format("GET {}", ROUTE_SNAPSHOT));
             snapPos != std::string_view::npos && 
             (request[snapPos + 13] == ' ' || request[snapPos + 13] == '?')) { // 13 is length of "GET /snapshot"
        
        client.closeAfterWrite = true;
        std::vector<uint8_t> snapshot = pipeline.getSnapshot();

        if (snapshot.empty()) {
            client.queueData(reinterpret_cast<const uint8_t*>(HTTP_NOT_FOUND.data()), HTTP_NOT_FOUND.size());
        } else {
            auto [it, size] = std::format_to_n(
                headerStackBuf, STACK_BUF_SIZE, HTTP_OK_JPEG_FMT, snapshot.size()
            );

            client.queueData(reinterpret_cast<const uint8_t*>(headerStackBuf), size);
            client.queueData(snapshot.data(), snapshot.size());
        }
    } 
    // 3. Favicon Route Matching
    std::string targetFaviconPath = std::format("GET {}", ROUTE_FAVICON);
    size_t favPos = request.find(targetFaviconPath);
    if (favPos != std::string_view::npos && 
       (request[favPos + targetFaviconPath.size()] == ' ' || request[favPos + targetFaviconPath.size()] == '?')) {
        
        client.closeAfterWrite = true;
        client.queueData(reinterpret_cast<const uint8_t*>(FAVICON_NOT_FOUND.data()), FAVICON_NOT_FOUND.size());
    } 
    // 4. Live Multi-Part Streaming Video Route
    else if (request.find("GET / HTTP") != std::string_view::npos || 
             request.find("GET /?") != std::string_view::npos) {
        client.isStreaming = true;
        client.sentFrameId = 0;
        client.closeAfterWrite = false; 
        client.queueData(reinterpret_cast<const uint8_t*>(HTTP_OK_MJPEG.data()), HTTP_OK_MJPEG.size());
    }
    // 5. Fallback Route Failure Catch
    else {
        client.closeAfterWrite = true;
        client.queueData(reinterpret_cast<const uint8_t*>(HTTP_NOT_FOUND.data()), HTTP_NOT_FOUND.size());
    }
    
    client.readBufferLen = 0;
}

void WebServer::broadcastLatestFrame() {
    uint32_t globalFrameId = 0;
    
    // 1. Thread-safe pull of the raw JPEG from the pipeline
    std::vector<uint8_t> currentFrame = pipeline.getCurrentFrame(globalFrameId);

    // If no new frames have arrived from the camera loop, yield instantly
    if (globalFrameId == localLatestFrameId || currentFrame.empty()) {
        return; 
    }

    localLatestFrameId = globalFrameId;

    // 2. Format the MJPEG multi-part header delimiter on the stack once per frame group
    char partHeaderBuf[STACK_BUF_SIZE];
    auto [formatIterator, headerSize] = std::format_to_n(
        partHeaderBuf, 
        STACK_BUF_SIZE, 
        MJPEG_FRAME_FMT, 
        currentFrame.size()
    );

    // 3. Thread-safe broad-distribution loop to all active connections
    std::scoped_lock<std::mutex> lock(clientsMutex);

    for (auto& client : *clients) {
        if (!client.isActive || !client.isStreaming) {
            continue;
        }
        if (client.outboxLen > ClientState::ENGINES_EXPECTED_FRAME_MAX * 3) {
            std::cerr << "[Network Core] Slow consumer detected on FD " << client.fileDescriptor << ". Evicting.\n";

            client.isActive = false; 
            client.outbox.clear();
            continue;
        }
        if (client.outboxLen == 0 && client.sentFrameId < globalFrameId) {
            
            // Queue Multipart Frame Header Boundary
            client.queueData(reinterpret_cast<const uint8_t*>(partHeaderBuf), headerSize);
            
            // Queue Raw Compressed JPEG Image Binary
            client.queueData(currentFrame.data(), currentFrame.size());
            
            // Track the last frame index delivered to this specific descriptor socket
            client.sentFrameId = globalFrameId;
        }
    }
}

void WebServer::handleWrite(int fileDescriptor) {
    bool shouldClose = false;

    {
        std::scoped_lock<std::mutex> lock(clientsMutex);
        
        auto it = std::find_if(clients->begin(), clients->end(), [fileDescriptor](const ClientState& c) {
            return c.isActive && c.fileDescriptor == fileDescriptor;
        });

        if (it == clients->end()) { 
            return; 
        }

        auto& client = *it;
        if (client.outbox.empty()) { 
            return; // Nothing waiting to be sent
        }

        // Flush outbox data using native vector size boundaries
        while (client.outboxOffset < client.outbox.size()) {
            size_t remaining = client.outbox.size() - client.outboxOffset;
            ssize_t sent = send(fileDescriptor, client.outbox.data() + client.outboxOffset, remaining, MSG_NOSIGNAL);

            if (sent > 0) {
                client.outboxOffset += sent;
            } else if (sent == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) { 
                    return; // Yield cleanly, socket's kernel write queue is full
                }
                shouldClose = true; // Hard network socket error
                break;
            }
        }

        // If the entire buffer has been flushed out successfully, reset everything
        if (!shouldClose && client.outboxOffset == client.outbox.size()) {
            client.outboxOffset = 0;
            client.outbox.clear(); // Safe and zero-overhead memory reclamation!

            if (client.closeAfterWrite) {
                shouldClose = true;
            }
        }
    } 

    if (shouldClose) {
        closeConnection(fileDescriptor);
    }
}

void WebServer::closeConnection(int fileDescriptor) {

    {
        std::scoped_lock<std::mutex> lock(clientsMutex);
        
        auto it = std::find_if(clients->begin(), clients->end(), [fileDescriptor](const ClientState& c) {
            return c.isActive && c.fileDescriptor == fileDescriptor;
        });

        if (it != clients->end()) {
            it->isActive = false;
            // Free any lingering vector allocation memory right away
            it->outbox.clear(); 
        }
    }

    shutdown(fileDescriptor, SHUT_WR);
    close(fileDescriptor);
}