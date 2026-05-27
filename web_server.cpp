#include "embedded_html.hpp"
#include "web_server.hpp"

#include <algorithm>
#include <charconv>
#include <csignal>
#include <cstring>
#include <iostream>
#include <format>

#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

WebServer::WebServer(int serverPort,
                     const std::atomic<bool>& runningFlag,
                     std::mutex& videoMutex,
                     const ByteVector& videoBuffer,
                     const uint32_t& videoFrameId,
                     std::mutex& snapMutex,
                     const ByteVector& snapBuffer)
    : port(serverPort),
      globalRunning(runningFlag),
      frameMutex(videoMutex),
      latestJpeg(videoBuffer),
      latestFrameId(videoFrameId),
      snapshotMutex(snapMutex),
      snapshotJpeg(snapBuffer),
      clients(std::make_unique<std::array<ClientState, MAX_CLIENTS>>()) {}

WebServer::~WebServer() {
    running = false;
    if (eventLoopThread.joinable()) {
        eventLoopThread.join();
    }
    if (listenFileDescriptor != -1) {
        close(listenFileDescriptor);
    }
}

bool WebServer::setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) { 
        return false; 
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
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
        std::cerr << std::format(ERR_BIND, port);
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

void WebServer::startEventLoop() {
    running = true;
    pollFileDescriptors.reserve(INITIAL_POLL_CAPACITY);
    eventLoopThread = std::thread(&WebServer::eventLoop, this);
}

void WebServer::eventLoop() {
    while (running && globalRunning) {
        broadcastLatestFrame();

        {
            std::lock_guard<std::mutex> lock(clientsMutex);

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

        std::lock_guard<std::mutex> lock(clientsMutex);

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
        {
            std::lock_guard<std::mutex> snapshotLock(snapshotMutex);

            if (snapshotJpeg.empty()) {
                queueData(
                    client, 
                    reinterpret_cast<const uint8_t*>(HTTP_NOT_FOUND.data()), HTTP_NOT_FOUND.size()
                );
            } else {
                auto [it, size] = std::format_to_n(
                    headerStackBuf, 
                    STACK_BUF_SIZE,
                    HTTP_OK_JPEG_FMT, 
                    snapshotJpeg.size()
                );

                queueData(client, reinterpret_cast<const uint8_t*>(headerStackBuf), size);
                queueData(client, snapshotJpeg.data(), snapshotJpeg.size());
            }
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
    std::lock_guard<std::mutex> lock(clientsMutex);
    uint32_t localLatestFrameId = latestFrameId;
    
    for (auto& client : *clients) {
        if (!client.isActive) {
            continue; 
        }

        if (client.isStreaming && client.sentFrameId != localLatestFrameId) {
            if (client.outboxLen > ServerConstants::TWO_MEGABYTES) { 
                continue; 
            }

            {
                std::lock_guard<std::mutex> frameLock(frameMutex);
                if (latestJpeg.empty()) { 
                    continue; 
                }

                queueData(
                    client, 
                    reinterpret_cast<const uint8_t*>(MJPEG_CHUNK_PREFIX.data()), 
                    MJPEG_CHUNK_PREFIX.size()
                );
                
                char num_buf[16];
                auto [ptr, ec] = std::to_chars(num_buf, num_buf + sizeof(num_buf), latestJpeg.size());
                if (ec == std::errc()) {
                    size_t num_len = ptr - num_buf;
                    queueData(client, reinterpret_cast<const uint8_t*>(num_buf), num_len);
                }

                queueData(
                    client, 
                    reinterpret_cast<const uint8_t*>(MJPEG_CHUNK_SUFFIX.data()), 
                    MJPEG_CHUNK_SUFFIX.size()
                );

                queueData(client, latestJpeg.data(), latestJpeg.size());

                queueData(
                    client, 
                    reinterpret_cast<const uint8_t*>(MJPEG_FOOTER.data()), 
                    MJPEG_FOOTER.size()
                );
            }

            client.sentFrameId = localLatestFrameId;
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
    std::lock_guard<std::mutex> lock(clientsMutex);
    
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