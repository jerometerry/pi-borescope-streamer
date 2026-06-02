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
#include <iostream>
#include "client_connection.hpp"
#include "device_finder.hpp"
#include "http_headers.hpp"
#include "http_routes.hpp"
#include "index_html.hpp"
#include "mjpeg_server.hpp"
#include "server_constants.hpp"
#include "shared_frame_pipeline.hpp"
#include "socket_errors.hpp"


MjpegServer::MjpegServer(const int port,
                     const std::atomic<bool>& running,
                     SharedFramePipeline& pipeline)
    : clients(std::make_unique<std::array<ClientConnection, ServerConstants::MAX_CLIENTS>>()),
      port(port),
      running(running),
      pipeline(pipeline) {
}

MjpegServer::~MjpegServer() {
    if (listenFileDescriptor != -1) {
        close(listenFileDescriptor);
        listenFileDescriptor = -1;
    }

    size_t activeConnectionsClosed = 0;
    {
        std::scoped_lock<std::mutex> lock(clientsMutex);
        
        for (auto& client : *clients) {
            if (client.isActive() && client.fd() != -1) {
                close(client.fd());
                client.evict();
                activeConnectionsClosed++;
            }
        }
    }
    
    if (activeConnectionsClosed > 0) {
        std::cout << "[Network Core] Evicted " << activeConnectionsClosed << " active sockets during lifecycle teardown.\n";
    }

    if (workerThread.joinable()) {
        workerThread.join();
    }
}

bool MjpegServer::initialize() {
    struct sigaction signalAction{};
    signalAction.sa_handler = SIG_IGN;
    sigemptyset(&signalAction.sa_mask);
    signalAction.sa_flags = 0;
    sigaction(SIGPIPE, &signalAction, nullptr);

    listenFileDescriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFileDescriptor == -1) {
        std::cerr << SocketErrors::ERR_SOCKET;
        return false;
    }

    int opt = 1;
    setsockopt(listenFileDescriptor, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
#if defined(__linux__) || defined(__APPLE__)
    setsockopt(listenFileDescriptor, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(listenFileDescriptor, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << std::format(SocketErrors::ERR_BIND, port);
        close(listenFileDescriptor);
        listenFileDescriptor = -1;
        return false;
    }

    if (listen(listenFileDescriptor, 10) < 0) {
        std::cerr << SocketErrors::ERR_LISTEN;
        close(listenFileDescriptor);
        listenFileDescriptor = -1;
        return false;
    }

    if (!setNonBlocking(listenFileDescriptor)) {
        std::cerr << SocketErrors::ERR_NONBLOCK;
        close(listenFileDescriptor);
        listenFileDescriptor = -1;
        return false;
    }

    for (auto& pfd : pollFds) {
        pfd.fd = -1;
        pfd.events = 0;
        pfd.revents = 0;
    }

    pollFds.front().fd = listenFileDescriptor;
    pollFds.front().events = POLLIN;

    return true;
}

void MjpegServer::start() {
    if (workerThread.joinable()) {
        std::cerr << "[Network Core Warning] MjpegServer::start() called on an already active engine instance.\n";
        return; 
    }

    std::cout << "[Network Core] Launching asynchronous engine worker thread...\n";
    workerThread = std::thread(&MjpegServer::eventLoop, this);
}

void MjpegServer::eventLoop() {
    while (running) {
        broadcastLatestFrame();

        {
            std::scoped_lock<std::mutex> lock(clientsMutex);

            for (size_t i = 0; i < ServerConstants::MAX_CLIENTS; ++i) {
                auto& client = clients->at(i);
                auto& pfd = pollFds.at(i + 1);

                if (client.isActive()) {
                    pfd.fd = client.fd();
                    pfd.events = POLLIN | (client.isOutboxEmpty() ? 0 : POLLOUT);
                } else {
                    pfd.fd = -1;
                }
            }
        }

        int ret = poll(pollFds.data(), pollFds.size(), 30);
        if (ret == -1) {
            if (errno == EINTR) continue;
            break;
        }

        if (pollFds.front().revents & POLLIN) {
            handleAccept();
        }

        for (size_t i = 1; i < pollFds.size(); ++i) {
            auto& pfd = pollFds.at(i);
            if (pfd.revents == 0 || pfd.fd == -1) continue;

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

bool MjpegServer::setNonBlocking(int fileDescriptor) {
    if (fileDescriptor < 0) {
        return false;
    }

    const int currentFlags = fcntl(fileDescriptor, F_GETFL, 0);
    if (currentFlags == -1) { 
        std::cerr << "[Network Utilities] Failed to retrieve socket flags on FD " << fileDescriptor << ".\n";
        return false; 
    }

    const int targetFlags = currentFlags | O_NONBLOCK;

    if (fcntl(fileDescriptor, F_SETFL, targetFlags) == -1) {
        std::cerr << "[Network Utilities] Failed to apply O_NONBLOCK flag on FD " << fileDescriptor << ".\n";
        return false;
    }

    return true;
}

void MjpegServer::handleAccept() {
    while (true) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);

        int fileDescriptor = accept(
            listenFileDescriptor, 
            reinterpret_cast<struct sockaddr*>(&clientAddr), 
            &clientLen
        );
        
        if (fileDescriptor == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) { 
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (!setNonBlocking(fileDescriptor)) {
            close(fileDescriptor);
            continue;
        }

        std::scoped_lock<std::mutex> lock(clientsMutex);

        auto it = std::find_if(
            clients->begin(), 
            clients->end(), 
            [](const ClientConnection& c) {
                return !c.isActive();
            });

        if (it != clients->end()) {
            it->activate(fileDescriptor);
        } else {
            std::string_view rejectionHeader = 
                "HTTP/1.1 503 Service Unavailable\r\n"
                "Content-Type: text/plain\r\n"
                "Connection: close\r\n\r\n"
                "Server Capacity Reached.\n";

            send(fileDescriptor, rejectionHeader.data(), rejectionHeader.size(), MSG_NOSIGNAL);
            shutdown(fileDescriptor, SHUT_WR);
            close(fileDescriptor);
        }
    }
}

void MjpegServer::handleRead(int fileDescriptor) {
    std::array<char, ClientConnection::READ_BUFFER_SIZE> temporaryBuffer{};
    ssize_t bytesRead = 0;

    while (true) {
        bytesRead = recv(fileDescriptor, temporaryBuffer.data(), temporaryBuffer.size(), 0);
        if (bytesRead == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            if (errno == EINTR) {
                continue;
            }
            closeConnection(fileDescriptor);
            return;
        }
        break;
    }

    if (bytesRead == 0) {
        closeConnection(fileDescriptor);
        return;
    }

    std::scoped_lock<std::mutex> lock(clientsMutex);

    auto it = std::find_if(
        clients->begin(), 
        clients->end(), 
        [fileDescriptor](const ClientConnection& c) {
            return c.isActive() && c.fd() == fileDescriptor;
        });

    if (it == clients->end()) { 
        return;
    }

    auto& client = *it;

    if (!client.appendToReadBuffer(temporaryBuffer.data(), static_cast<size_t>(bytesRead))) {
        std::cerr << "[Network Core] Input buffer overflow on FD " << fileDescriptor << ". Terminating.\n";
        client.evict();
        return;
    }

    std::string_view incoming(client.readData(), client.readLen());
    if (incoming.find(HttpHeaders::HEADER_DELIMITER) != std::string_view::npos) {
        processClientRequest(client);
    }
}



void MjpegServer::processClientRequest(ClientConnection& client) const {
    std::string_view request(client.readData(), client.readLen());

     if (HttpRoutes::requestMatchesRoute(request, HttpRoutes::ROUTE_DASHBOARD)) {
        if (HttpRoutes::exactMatch(request, HttpRoutes::ROUTE_DASHBOARD)) {
            client.setCloseAfterWrite(true);
            std::string_view htmlPageContent = Resources::index_html;

            client.queueHttpOkResponse(htmlPageContent);
            client.resetReadBuffer();
            return; 
        }
    }

    if (HttpRoutes::requestMatchesRoute(request, HttpRoutes::ROUTE_CAMERAS)) {
        if (HttpRoutes::exactMatch(request, HttpRoutes::ROUTE_CAMERAS)) {
            client.setCloseAfterWrite(true);
            auto cameras = DeviceFinder::superCameras();
            std::string jsonPayload = DeviceFinder::toJson(cameras);

            client.queueResponse(
                HttpHeaders::HTTP_OK_JSON_HDR, 
                jsonPayload, 
                HttpHeaders::HTTP_HDR_END
            );
            
            return;
        }
    }
    
    if (HttpRoutes::requestMatchesRoute(request, HttpRoutes::ROUTE_SNAPSHOT)) {
        if (HttpRoutes::exactMatch(request, HttpRoutes::ROUTE_SNAPSHOT)) {
            client.setCloseAfterWrite(true);
            std::shared_ptr<const std::vector<uint8_t>> snapshot = pipeline.getSnapshot();

            if (!snapshot || snapshot->empty()) {
                client.queueData(HttpHeaders::HTTP_NOT_FOUND);
            } else {
                client.queueJpegOkResponse(*snapshot);
            }
            client.resetReadBuffer();
            return;
        }
    }

    if (HttpRoutes::requestMatchesRoute(request, HttpRoutes::ROUTE_FAVICON)) {
        if (HttpRoutes::exactMatch(request, HttpRoutes::ROUTE_FAVICON)) {
            client.setCloseAfterWrite(true);
            client.queueData(HttpHeaders::FAVICON_NOT_FOUND);
            client.resetReadBuffer();
            return;
        }
    } 
    
    if (HttpRoutes::requestMatchesRoute(request, HttpRoutes::ROUTE_STREAM) || 
        HttpRoutes::requestMatchesRoute(request, HttpRoutes::ROUTE_STREAM_QUERY)) {
        client.setStreaming(true);
        client.setSentFrameId(0);
        client.setCloseAfterWrite(false); 
        client.queueData(HttpHeaders::HTTP_OK_MJPEG);
    }
    else {
        client.setCloseAfterWrite(true);
        client.queueData(HttpHeaders::HTTP_NOT_FOUND);
    }
    
    client.resetReadBuffer();
}

void MjpegServer::broadcastLatestFrame() {
    uint32_t currentFrameId = 0;
    std::shared_ptr<const std::vector<uint8_t>> currentFrame = pipeline.getCurrentFrame(currentFrameId);

    if (!currentFrame || currentFrame->empty() || currentFrameId == lastBroadcastedFrameId) {
        return;
    }

    lastBroadcastedFrameId = currentFrameId;
    std::scoped_lock<std::mutex> lock(clientsMutex);
    char partHeaderBuf[ServerConstants::STACK_BUF_SIZE];

    for (auto& client : *clients) {
        if (!client.isActive() || !client.isStreaming()) {
            continue;
        }
        
        if (client.outboxSize() > ClientConnection::MAX_OUTBOX_CAPACITY) {
            std::cerr << "[Network Core] Slow consumer detected on FD " << client.fd() << ". Evicting.\n";
            client.evict();
            continue;
        }

        if (client.isOutboxEmpty() && client.sentFrameId() < currentFrameId) {
            client.queueData(HttpHeaders::MJPEG_CHUNK_PREFIX);

            std::to_chars_result result = std::to_chars(
                partHeaderBuf, 
                partHeaderBuf + ServerConstants::STACK_BUF_SIZE, 
                currentFrame->size()
            );

            client.queueData(partHeaderBuf, result);
            client.queueData(HttpHeaders::MJPEG_CHUNK_SUFFIX);
            client.queueData(*currentFrame);

            client.setSentFrameId(currentFrameId);
        }
    }
}

void MjpegServer::handleWrite(int fileDescriptor) {
    bool shouldDisconnectSocket = false;
    ClientConnection* clientToDisconnect = nullptr;

    {
        std::scoped_lock<std::mutex> lock(clientsMutex);
        
        auto it = std::find_if(clients->begin(), clients->end(), [fileDescriptor](const ClientConnection& c) {
            return c.isActive() && c.fd() == fileDescriptor;
        });

        if (it == clients->end()) { 
            return; 
        }

        ClientConnection::WriteStatus status = it->flushOutbox();
        
        if (status == ClientConnection::WriteStatus::ClosedOrError) {
            shouldDisconnectSocket = true;
            clientToDisconnect = &(*it); // Capture the pointer to clear the FD tracking later
        }
    }

    if (shouldDisconnectSocket) {
        shutdown(fileDescriptor, SHUT_WR);
        close(fileDescriptor);

        std::scoped_lock<std::mutex> lock(clientsMutex);
        if (clientToDisconnect) {
            if (!clientToDisconnect->isActive() && clientToDisconnect->fd() == fileDescriptor) {
                clientToDisconnect->activate(-1);
                clientToDisconnect->evict();
            }
        }
    }
}


void MjpegServer::closeConnection(int fileDescriptor) {
    {
        std::scoped_lock<std::mutex> lock(clientsMutex);
        
        auto it = std::find_if(
            clients->begin(), 
            clients->end(), 
            [fileDescriptor](const ClientConnection& c) {
                return c.isActive() && c.fd() == fileDescriptor;
            });

        if (it != clients->end()) {
            it->evict(); 
        }
    }

    shutdown(fileDescriptor, SHUT_WR);
    close(fileDescriptor);
}