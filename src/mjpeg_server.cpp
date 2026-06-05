#include <App.h>
#include <HttpResponse.h>
#include <Loop.h>
#include <libusockets.h>
#include <algorithm>
#include <charconv>
#include <cstddef>
#include <iostream>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include "constants.hpp"
#include "data_structures.hpp"
#include "device_finder.hpp"
#include "index_html.hpp"
#include "mjpeg_server.hpp"

MjpegServer::MjpegServer(const int port, const std::atomic<bool>& running, FrameSource frameSource)
    : port_(port), running_(running), frameSource_(std::move(frameSource)) {}

MjpegServer::~MjpegServer() {
    if (networkThread_.joinable()) {
        networkThread_.join();
    }
    std::cout << "[Network Core] Network engine cleanly terminated.\n";
}

void MjpegServer::onTimer(us_timer_t *t) {
    auto* server = *static_cast<MjpegServer**>(us_timer_ext(t));

    if (!server->running_) {
        for (auto& viewer : server->activeViewers_) {
            if (!viewer.isClosed) {
                viewer.res->close();
            }
        }
        server->activeViewers_.clear();

        if (server->listenSocket_) {
            constexpr int GRACEFUL_CLOSE = 0;
            us_listen_socket_close(GRACEFUL_CLOSE, server->listenSocket_);
            server->listenSocket_ = nullptr;
        }
        us_timer_close(t);
        return;
    }

    uint32_t currentFrameId = 0;
    auto currentFrame = server->frameSource_(currentFrameId);

    if (currentFrame && !currentFrame->empty() && currentFrameId != server->lastBroadcastedFrameId_) {
        server->lastBroadcastedFrameId_ = currentFrameId;

        for (size_t i = 0; i < server->activeViewers_.size(); ) {
            auto& viewer = server->activeViewers_[i];
            auto* res = viewer.res;

            if (viewer.isClosed) {
                server->activeViewers_.erase(server->activeViewers_.begin() + i);
                continue;
            }

            if (res->getWriteOffset() > WebServerConfig::MAX_OUTGOING_CLIENT_BUFFER_SIZE) {
                std::cerr << "[Network Core] Evicting lagging viewer on /stream.\n";
                res->end();
                server->activeViewers_.erase(server->activeViewers_.begin() + i);
                continue;
            }

            if (viewer.lastSentFrameId < currentFrameId) {
                size_t backpressure = res->getWriteOffset();

                if (backpressure == 0) {
                    if (viewer.isLagging) {
                        uint32_t droppedFrames = currentFrameId - viewer.lagStartFrameId;
                        std::cout << "[Network Telemetry] Viewer recovered. TCP pipe cleared. " 
                                  << droppedFrames << " frames were deliberately dropped to maintain real-time latency.\n";
                        viewer.isLagging = false;
                    }

                    res->cork([&]() {
                        res->write(HttpHeaders::MJPEG_CHUNK_PREFIX);
                        
                        char headerBuf[WebServerConfig::HEADER_BUFFER_SIZE];

                        auto result = std::to_chars(
                            headerBuf, 
                            headerBuf + WebServerConfig::HEADER_BUFFER_SIZE, 
                            currentFrame->size()
                        );

                        res->write(std::string_view(headerBuf, result.ptr - headerBuf));
                        
                        res->write(HttpHeaders::MJPEG_CHUNK_SUFFIX);
                        res->write(std::string_view(reinterpret_cast<const char*>(currentFrame->data()), currentFrame->size()));
                    });
                } else {
                    if (!viewer.isLagging) {
                        std::cout << "[Network Telemetry] Warning: TCP stall detected! OS buffer backed up with " 
                                  << backpressure << " bytes. Dropping frames...\n";
                        viewer.isLagging = true;
                        viewer.lagStartFrameId = currentFrameId;
                    }
                }

                viewer.lastSentFrameId = currentFrameId;
            }
            ++i;
        }
    }
}

void MjpegServer::start() {
    std::promise<void> loopPromise;
    auto loopFuture = loopPromise.get_future();

    networkThread_ = std::thread([this, &loopPromise]() {
        uWS::App app;
        
        app.get("/", [](auto *res, auto *) {
            res->writeHeader("Connection", "close")
               ->writeHeader("Content-Type", "text/html")
               ->end(Resources::index_html);
        });

        app.get("/api/cameras", [](auto *res, auto *) {
            auto cameras = DeviceFinder::superCameras();
            std::string jsonPayload = DeviceFinder::toJson(cameras);
            res->writeHeader("Connection", "close")
               ->writeHeader("Content-Type", "application/json")
               ->end(jsonPayload);
        });
        
        app.get("/favicon.ico", [](auto *res, auto *) {
            res->writeStatus("404 Not Found")
               ->writeHeader("Connection", "close")
               ->writeHeader("Cache-Control", "public, max-age=31536000")
               ->end();
        });

        app.get("/stream", [this](auto *res, auto *) {
            if (activeViewers_.size() >= WebServerConfig::MAX_CLIENTS) {
                res->writeStatus("503 Service Unavailable")->end("Server Capacity Reached");
                return;
            }

            res->writeStatus("200 OK")
               ->writeHeader("Connection", "close")
               ->writeHeader("Cache-Control", "no-cache, private")
               ->writeHeader("Pragma", "no-cache")
               ->writeHeader("Content-Type", "multipart/x-mixed-replace; boundary=mjpegstream");

            activeViewers_.push_back({res, 0, false});

            res->onAborted([this, res]() {
                auto it = std::find_if(activeViewers_.begin(), activeViewers_.end(),
                    [res](const Web::ViewerState& v) { return v.res == res; });
                    
                if (it != activeViewers_.end()) {
                    it->isClosed = true;
                }
            });
        });

        app.any("/*", [](auto *res, auto *) {
            res->writeStatus("404 Not Found")->end();
        });

        app.listen(port_, [this](us_listen_socket_t *socket) {
            if (socket) {
                listenSocket_ = socket;
                std::cout << "[Network Core] Asynchronous uWebSockets engine listening on port " << port_ << '\n';

                auto *loop = reinterpret_cast<struct us_loop_t *>(uWS::Loop::get());
                us_timer_t *timer = us_create_timer(
                    loop, 
                    WebServerConfig::TIMER_FALLTHROUGH, 
                    sizeof(MjpegServer*)
                );
                
                *static_cast<MjpegServer**>(us_timer_ext(timer)) = this;
                us_timer_set(
                    timer, 
                    MjpegServer::onTimer, 
                    WebServerConfig::TIMER_INTERVAL_MS, 
                    WebServerConfig::TIMER_INTERVAL_MS
                );
            } else {
                std::cerr << "[Network Core Error] Failed to bind to port " << port_ << '\n';
            }
        });

        loopPromise.set_value(); 
        app.run();
    });

    loopFuture.wait();
}