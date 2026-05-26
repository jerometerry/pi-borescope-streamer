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
    : port_(serverPort),
      globalRunning(runningFlag),
      frameMutex(videoMutex),
      latestJpeg(videoBuffer),
      latestFrameId(videoFrameId),
      snapshotMutex(snapMutex),
      snapshotJpeg(snapBuffer),
      clients_(std::make_unique<std::array<ClientState, MAX_CLIENTS>>()) {}

WebServer::~WebServer() {
    running_ = false;
    if (event_loop_thread_.joinable()) {
        event_loop_thread_.join();
    }
    if (listen_fd_ != -1) {
        close(listen_fd_);
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

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ == -1) {
        std::cerr << ERR_SOCKET;
        return false;
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);

    if (bind(listen_fd_, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << std::format(ERR_BIND, port_);
        return false;
    }

    if (listen(listen_fd_, 10) < 0) {
        std::cerr << ERR_LISTEN;
        return false;
    }

    if (!setNonBlocking(listen_fd_)) {
        std::cerr << ERR_NONBLOCK;
        return false;
    }

    return true;
}

void WebServer::startEventLoop() {
    running_ = true;
    poll_fds_.reserve(INITIAL_POLL_CAPACITY);
    event_loop_thread_ = std::thread(&WebServer::eventLoop, this);
}

void WebServer::eventLoop() {
    while (running_ && globalRunning) {
        broadcastLatestFrame();

        {
            std::lock_guard<std::mutex> lock(clients_mutex_);

            poll_fds_.clear();
            poll_fds_.push_back({listen_fd_, POLLIN, 0});
            
            for (const auto& client : *clients_) {
                if (!client.is_active) {
                    continue;
                }
                short events = POLLIN;
                if (client.outbox_len > 0) {
                    events |= POLLOUT;
                }
                poll_fds_.push_back({client.fd, events, 0});
            }
        }

        int ret = poll(poll_fds_.data(), poll_fds_.size(), 10);
        if (ret == -1) {
            if (errno == EINTR) { 
                continue; 
            }
            break;
        }

        for (const auto& pfd : poll_fds_) {
            if (pfd.revents == 0) { 
                continue; 
            }

            if (pfd.fd == listen_fd_) {
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
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd_, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) { 
                break; 
            }
            break;
        }

        if (!setNonBlocking(client_fd)) {
            close(client_fd);
            continue;
        }

        std::lock_guard<std::mutex> lock(clients_mutex_);

        auto it = std::find_if(clients_->begin(), clients_->end(), [](const ClientState& c) {
            return !c.is_active;
        });

        if (it != clients_->end()) {
            it->reset(client_fd);
        } else {
            close(client_fd);
        }
    }
}

void WebServer::handleRead(int client_fd) {
    auto it = std::find_if(clients_->begin(), clients_->end(), [client_fd](const ClientState& c) {
        return c.is_active && c.fd == client_fd;
    });

    if (it == clients_->end()) { 
        return; 
    }

    auto& client = *it;
    
    while (true) {
        size_t available_space = client.read_buffer.size() - client.read_buffer_len;
        if (available_space == 0) {
            closeConnection(client_fd);
            return;
        }

        ssize_t bytes_read = recv(client_fd, client.read_buffer.data() + client.read_buffer_len, available_space, 0);

        if (bytes_read > 0) {
            client.read_buffer_len += bytes_read;
            std::string_view stream_view(client.read_buffer.data(), client.read_buffer_len);

            if (stream_view.find(HEADER_DELIMITER) != std::string_view::npos) {
                processClientRequest(client); // Ensure processClientRequest matches this string_view pattern
                break;
            }
        } else if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) { 
                break; 
            }
            closeConnection(client_fd);
            return;
        } else {
            closeConnection(client_fd);
            return;
        }
    }
}


void WebServer::processClientRequest(ClientState& client) {
    std::string_view request_view(client.read_buffer.data(), client.read_buffer_len);

    if (request_view.find(ROUTE_WEB) != std::string_view::npos) {
        client.close_after_write = true;

        auto [it, size] = std::format_to_n(
            header_stack_buf_, 
            STACK_BUF_SIZE, 
            HTTP_OK_HTML_FMT, 
            htmlPageContent.size()
        );

        queueData(client, reinterpret_cast<const uint8_t*>(header_stack_buf_), size);
        queueData(client, reinterpret_cast<const uint8_t*>(htmlPageContent.data()), htmlPageContent.size());
    } 
    else if (request_view.find(ROUTE_SNAPSHOT) != std::string_view::npos) {
        client.close_after_write = true;
        {
            std::lock_guard<std::mutex> snapshotLock(snapshotMutex);

            if (snapshotJpeg.empty()) {
                queueData(
                    client, 
                    reinterpret_cast<const uint8_t*>(HTTP_NOT_FOUND.data()), HTTP_NOT_FOUND.size()
                );
            } else {
                auto [it, size] = std::format_to_n(
                    header_stack_buf_, 
                    STACK_BUF_SIZE,
                    HTTP_OK_JPEG_FMT, 
                    snapshotJpeg.size()
                );

                queueData(client, reinterpret_cast<const uint8_t*>(header_stack_buf_), size);
                queueData(client, snapshotJpeg.data(), snapshotJpeg.size());
            }
        }
    } 
    else if (request_view.find(ROUTE_FAVICON) != std::string_view::npos) {
        client.close_after_write = true;
        queueData(client, reinterpret_cast<const uint8_t*>(FAVICON_NOT_FOUND.data()), FAVICON_NOT_FOUND.size());
    } 
    else if (request_view.find("GET / HTTP") != std::string_view::npos || 
             request_view.find("GET /?") != std::string_view::npos) {
        client.is_streaming = true;
        client.sent_frame_id = 0;
        queueData(client, reinterpret_cast<const uint8_t*>(HTTP_OK_MJPEG.data()), HTTP_OK_MJPEG.size());
    }
    else {
        client.close_after_write = true;
        queueData(client, reinterpret_cast<const uint8_t*>(HTTP_NOT_FOUND.data()), HTTP_NOT_FOUND.size());
    }
    
    client.read_buffer_len = 0;

    if (client.close_after_write && client.outbox.empty()) {
        closeConnection(client.fd);
    }
}

void WebServer::broadcastLatestFrame() {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    uint32_t localLatestFrameId = latestFrameId;
    
    for (auto& client : *clients_) {
        if (!client.is_active) {
            continue; 
        }

        if (client.is_streaming && client.sent_frame_id != localLatestFrameId) {
            if (client.outbox_len > ServerConstants::TWO_MEGABYTES) { 
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

            client.sent_frame_id = localLatestFrameId;
        }
    }
}

void WebServer::queueData(ClientState& client, const uint8_t* data, size_t size) {
    if (!client.is_active) {
        return; 
    }

    if (client.outbox_len == 0) {
        ssize_t sent = send(client.fd, data, size, MSG_NOSIGNAL);
        if (sent >= 0) {
            if (static_cast<size_t>(sent) == size) { 
                return;
            }
            size_t remaining = size - sent;
            if (remaining > client.outbox.size()) {
                closeConnection(client.fd);
                return;
            }
            std::memcpy(client.outbox.data(), data + sent, remaining);
            client.outbox_len = remaining;
            client.outbox_offset = 0;
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                closeConnection(client.fd);
                return;
            }
            if (size > client.outbox.size()) {
                closeConnection(client.fd);
                return;
            }
            std::memcpy(client.outbox.data(), data, size);
            client.outbox_len = size;
            client.outbox_offset = 0;
        }
    } else {
        if (client.outbox_len + size > client.outbox.size()) {
            // Buffer overflow safety guard
            closeConnection(client.fd);
            return;
        }
        std::memcpy(client.outbox.data() + client.outbox_len, data, size);
        client.outbox_len += size;
    }
}

void WebServer::handleWrite(int client_fd) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    auto it = std::find_if(clients_->begin(), clients_->end(), [client_fd](const ClientState& c) {
        return c.is_active && c.fd == client_fd;
    });

    if (it == clients_->end()) { 
        return; 
    }

    auto& client = *it;
    if (client.outbox_len == 0) { 
        return; 
    }

    while (client.outbox_offset < client.outbox_len) {
        size_t remaining = client.outbox_len - client.outbox_offset;
        ssize_t sent = send(client_fd, client.outbox.data() + client.outbox_offset, remaining, MSG_NOSIGNAL);

        if (sent > 0) {
            client.outbox_offset += sent;
        } else if (sent == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) { 
                return; 
            }
            closeConnection(client_fd);
            return;
        }
    }

    if (client.outbox_offset >= client.outbox_len) {
        client.outbox_len = 0;
        client.outbox_offset = 0;
    }

    if (client.close_after_write) {
        closeConnection(client_fd);
    }
}

void WebServer::closeConnection(int client_fd) {
    auto it = std::find_if(clients_->begin(), clients_->end(), [client_fd](const ClientState& c) {
        return c.is_active && c.fd == client_fd;
    });

    if (it != clients_->end()) {
        it->is_active = false;
    }

    shutdown(client_fd, SHUT_WR);
    close(client_fd);
}