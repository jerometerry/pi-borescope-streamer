#include <arpa/inet.h> 
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <csignal>
#include <cstring>
#include <format>
#include <iostream>
#include <system_error>
#include "client_state.hpp"
#include "index_html.hpp"
#include "server_constants.hpp"
#include "web_server.hpp"

WebServer::WebServer(const int port,
                     const std::atomic<bool>& running,                 
                     SharedFramePipeline& pipeline)
    : clients(std::make_unique<std::array<ClientState, MAX_CLIENTS>>()),
      port(port),
      running(running),
      pipeline(pipeline) {}

WebServer::~WebServer() {
    if (workerThread.joinable()) {
        workerThread.join();
    }
    if (listenFileDescriptor != -1) {
        close(listenFileDescriptor);
    }
}

bool WebServer::initialize() {
    // IGNORING SIGPIPE: Prevents macOS from instantly killing the application 
    // when attempting to send data to a client that disconnected abruptly.
    signal(SIGPIPE, SIG_IGN);

    listenFileDescriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFileDescriptor == -1) {
        std::cerr << ERR_SOCKET;
        return false;
    }

    int opt = 1;
    setsockopt(listenFileDescriptor, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(listenFileDescriptor, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Error binding to port: " << port << '\n';
        return false;
    }

    if (listen(listenFileDescriptor, 10) < 0) {
        std::cerr << ERR_LISTEN;
        return false;
    }

    if (!setNonBlocking(listenFileDescriptor)) {
        std::cerr << ERR_NONBLOCK;
        return false;
    }

    return true;
}

void WebServer::start() {
    pollFileDescriptors.reserve(INITIAL_POLL_CAPACITY);
    workerThread = std::thread(&WebServer::eventLoop, this);
}

void WebServer::eventLoop() {
    while (running) {
        broadcastLatestFrame();

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
                    events |= POLLOUT;
                }
                pollFileDescriptors.push_back({client.fileDescriptor, events, 0});
            }
        }

        int ret = poll(pollFileDescriptors.data(), pollFileDescriptors.size(), 10);
        if (ret == -1) {
            if (errno == EINTR) { 
                continue; 
            }
            break;
        }

        for (const auto& pfd : pollFileDescriptors) {
            if (pfd.revents == 0) { 
                continue; 
            }

            if (pfd.fd == listenFileDescriptor) {
                if (pfd.revents & POLLIN) { 
                    handleAccept(); 
                }
            } else {
                if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    closeConnection(pfd.fd);
                } else {
                    if (pfd.revents & POLLIN) { 
                        handleRead(pfd.fd); 
                    }
                    if (pfd.revents & POLLOUT) { 
                        handleWrite(pfd.fd); 
                    }
                }
            }
        }
    }
}

bool WebServer::setNonBlocking(int fileDescriptor) {
    int flags = fcntl(fileDescriptor, F_GETFL, 0);
    if (flags == -1) { 
        return false; 
    }
    return fcntl(fileDescriptor, F_SETFL, flags | O_NONBLOCK) == 0;
}

void WebServer::handleAccept() {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t clientLen = sizeof(client_addr);
        int fileDescriptor = accept(listenFileDescriptor, (struct sockaddr*)&client_addr, &clientLen);
        
        if (fileDescriptor == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) { 
                break; 
            }
            break;
        }

        if (!setNonBlocking(fileDescriptor)) {
            close(fileDescriptor);
            continue;
        }

        std::scoped_lock<std::mutex> lock(clientsMutex);

        auto it = std::find_if(clients->begin(), clients->end(), [](const ClientState& c) {
            return !c.isActive;
        });

        if (it != clients->end()) {
            it->reset(fileDescriptor);
        } else {
            close(fileDescriptor);
        }
    }
}

void WebServer::handleRead(int fileDescriptor) {
    auto it = std::find_if(clients->begin(), clients->end(), [fileDescriptor](const ClientState& c) {
        return c.isActive && c.fileDescriptor == fileDescriptor;
    });

    if (it == clients->end()) { 
        return; 
    }

    auto& client = *it;
    
    while (true) {
        size_t availableSpace = client.readBuffer.size() - client.readBufferLen;
        if (availableSpace == 0) {
            closeConnection(fileDescriptor);
            return;
        }

        ssize_t bytesRead = recv(fileDescriptor, client.readBuffer.data() + client.readBufferLen, availableSpace, 0);

        if (bytesRead > 0) {
            client.readBufferLen += bytesRead;
            std::string_view incoming(client.readBuffer.data(), client.readBufferLen);

            if (incoming.find(HEADER_DELIMITER) != std::string_view::npos) {
                processClientRequest(client);
                break;
            }
        } else if (bytesRead == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) { 
                break; 
            }
            closeConnection(fileDescriptor);
            return;
        } else {
            closeConnection(fileDescriptor);
            return;
        }
    }
}


void WebServer::processClientRequest(ClientState& client) {
    std::string_view request(client.readBuffer.data(), client.readBufferLen);

    if (request.find(ROUTE_WEB) != std::string_view::npos) {
        client.closeAfterWrite = true;

        auto htmlPageContent = Resources::index_html;

        auto [it, size] = std::format_to_n(
            headerStackBuf, 
            STACK_BUF_SIZE, 
            HTTP_OK_HTML_FMT, 
            htmlPageContent.size()
        );

        queueData(client, reinterpret_cast<const uint8_t*>(headerStackBuf), size);
        queueData(client, reinterpret_cast<const uint8_t*>(htmlPageContent.data()), htmlPageContent.size());
    } 
    else if (request.find(ROUTE_SNAPSHOT) != std::string_view::npos) {
        client.closeAfterWrite = true;
    
        std::vector<uint8_t> snapshot = pipeline.getSnapshot();

        if (snapshot.empty()) {
            queueData(client, reinterpret_cast<const uint8_t*>(HTTP_NOT_FOUND.data()), HTTP_NOT_FOUND.size());
        } else {
            auto [it, size] = std::format_to_n(
                headerStackBuf, 
                STACK_BUF_SIZE,
                HTTP_OK_JPEG_FMT, 
                snapshot.size()
            );

            queueData(client, reinterpret_cast<const uint8_t*>(headerStackBuf), size);
            queueData(client, snapshot.data(), snapshot.size());
        }
    } 
    else if (request.find(ROUTE_FAVICON) != std::string_view::npos) {
        client.closeAfterWrite = true;
        queueData(client, reinterpret_cast<const uint8_t*>(FAVICON_NOT_FOUND.data()), FAVICON_NOT_FOUND.size());
    } 
    else if (request.find("GET / HTTP") != std::string_view::npos || 
             request.find("GET /?") != std::string_view::npos) {
        client.isStreaming = true;
        client.sentFrameId = 0;
        queueData(client, reinterpret_cast<const uint8_t*>(HTTP_OK_MJPEG.data()), HTTP_OK_MJPEG.size());
    }
    else {
        client.closeAfterWrite = true;
        queueData(client, reinterpret_cast<const uint8_t*>(HTTP_NOT_FOUND.data()), HTTP_NOT_FOUND.size());
    }
    
    client.readBufferLen = 0;

    if (client.closeAfterWrite && client.outbox.empty()) {
        closeConnection(client.fileDescriptor);
    }
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

    // 2. Format the MJPEG multi-part header delimiter on the stack
    // Presumes standard format string, e.g.: "\r\n--boundary\r\nContent-Type: image/jpeg\r\nContent-Length: {}\r\n\r\n"
    char partHeaderBuf[STACK_BUF_SIZE];
    auto [it, headerSize] = std::format_to_n(
        partHeaderBuf, 
        STACK_BUF_SIZE, 
        MJPEG_FRAME_FMT, 
        currentFrame.size()
    );

    // 3. Thread-safe broad-distribution loop to all active connections
    std::scoped_lock<std::mutex> lock(clientsMutex);

    for (auto& client : *clients) {
        // Only stream to valid HTTP clients who requested the live video route
        if (!client.isActive || !client.isStreaming) {
            continue;
        }

        // Prevent slow-network socket consumers from bloating memory indefinitely.
        // If an outbox grows too large, drop the connection to maintain systemic low latency.
        if (client.outboxLen > ClientState::ENGINES_EXPECTED_FRAME_MAX * 3) {
            std::cerr << "[Network Core] Slow consumer detected on FD " << client.fileDescriptor << ". Dropping stream.\n";
            // Set flag or close directly. We can clear its state so the loop skips it.
            close(client.fileDescriptor);
            client.isActive = false;
            continue;
        }

        // Only send this frame if the client has completely finished sending the old one.
        // If a client's outbox is still flushing, it's safer to drop this frame *for this client only* 
        // to prevent lag or network buffer bloat.
        if (client.outboxLen == 0 && client.sentFrameId < globalFrameId) {
            
            // Queue Multipart Frame Header Boundary
            queueData(client, reinterpret_cast<const uint8_t*>(partHeaderBuf), headerSize);
            
            // Queue Raw Compressed JPEG Image Binary
            queueData(client, currentFrame.data(), currentFrame.size());
            
            // Track the last frame index delivered to this specific descriptor socket
            client.sentFrameId = globalFrameId;
        }
    }
}



void WebServer::queueData(ClientState& client, const uint8_t* data, size_t size) {
    if (!client.isActive) {
        return; 
    }

    if (client.outboxLen == 0) {
        ssize_t sent = send(client.fileDescriptor, data, size, MSG_NOSIGNAL);
        if (sent >= 0) {
            if (static_cast<size_t>(sent) == size) { 
                return;
            }
            size_t remaining = size - sent;
            if (remaining > client.outbox.size()) {
                closeConnection(client.fileDescriptor);
                return;
            }
            std::memcpy(client.outbox.data(), data + sent, remaining);
            client.outboxLen = remaining;
            client.outboxOffset = 0;
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                closeConnection(client.fileDescriptor);
                return;
            }
            if (size > client.outbox.size()) {
                closeConnection(client.fileDescriptor);
                return;
            }
            std::memcpy(client.outbox.data(), data, size);
            client.outboxLen = size;
            client.outboxOffset = 0;
        }
    } else {
        if (client.outboxLen + size > client.outbox.size()) {
            closeConnection(client.fileDescriptor);
            return;
        }
        std::memcpy(client.outbox.data() + client.outboxLen, data, size);
        client.outboxLen += size;
    }
}

void WebServer::handleWrite(int fileDescriptor) {
    std::scoped_lock<std::mutex> lock(clientsMutex);
    
    auto it = std::find_if(clients->begin(), clients->end(), [fileDescriptor](const ClientState& c) {
        return c.isActive && c.fileDescriptor == fileDescriptor;
    });

    if (it == clients->end()) { 
        return; 
    }

    auto& client = *it;
    if (client.outboxLen == 0) { 
        return; 
    }

    while (client.outboxOffset < client.outboxLen) {
        size_t remaining = client.outboxLen - client.outboxOffset;
        ssize_t sent = send(fileDescriptor, client.outbox.data() + client.outboxOffset, remaining, MSG_NOSIGNAL);

        if (sent > 0) {
            client.outboxOffset += sent;
        } else if (sent == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) { 
                return; 
            }
            closeConnection(fileDescriptor);
            return;
        }
    }

    if (client.outboxOffset >= client.outboxLen) {
        client.outboxLen = 0;
        client.outboxOffset = 0;
    }

    if (client.closeAfterWrite) {
        closeConnection(fileDescriptor);
    }
}

void WebServer::closeConnection(int fileDescriptor) {
    auto it = std::find_if(clients->begin(), clients->end(), [fileDescriptor](const ClientState& c) {
        return c.isActive && c.fileDescriptor == fileDescriptor;
    });

    if (it != clients->end()) {
        it->isActive = false;
    }

    shutdown(fileDescriptor, SHUT_WR);
    close(fileDescriptor);
}