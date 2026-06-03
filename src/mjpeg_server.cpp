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
#include "device_finder.hpp"
#include "http_headers.hpp"
#include "index_html.hpp"
#include "mjpeg_server.hpp"
#include "server_constants.hpp"
#include "shared_frame_pipeline.hpp"

MjpegServer::MjpegServer(const int port, const std::atomic<bool>& running, SharedFramePipeline& pipeline)
    : port(port), running(running), pipeline(pipeline) {}

MjpegServer::~MjpegServer() {
    if (networkThread.joinable()) {
        networkThread.join();
    }
    std::cout << "[Network Core] Network engine cleanly terminated.\n";
}

void MjpegServer::onTimer(us_timer_t *t) {
    auto* server = *static_cast<MjpegServer**>(us_timer_ext(t));

    if (!server->running) {
        for (auto& viewer : server->activeViewers) {
            if (!viewer.isClosed) {
                viewer.res->close();
            }
        }
        server->activeViewers.clear();

        if (server->listenSocket) {
            constexpr int GRACEFUL_CLOSE = 0;
            us_listen_socket_close(GRACEFUL_CLOSE, server->listenSocket);
            server->listenSocket = nullptr;
        }
        us_timer_close(t);
        return;
    }

    uint32_t currentFrameId = 0;
    auto currentFrame = server->pipeline.getCurrentFrame(currentFrameId);

    if (currentFrame && !currentFrame->empty() && currentFrameId != server->lastBroadcastedFrameId) {
        server->lastBroadcastedFrameId = currentFrameId;

        for (size_t i = 0; i < server->activeViewers.size(); ) {
            auto& viewer = server->activeViewers[i];
            auto* res = viewer.res;

            if (viewer.isClosed) {
                server->activeViewers.erase(server->activeViewers.begin() + i);
                continue;
            }

            if (res->getWriteOffset() > ServerConstants::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES) {
                std::cerr << "[Network Core] Evicting lagging viewer on /stream.\n";
                res->end();
                server->activeViewers.erase(server->activeViewers.begin() + i);
                continue;
            }

            if (viewer.lastSentFrameId < currentFrameId) {
                size_t backpressure = res->getWriteOffset();

                if (backpressure == 0) {
                    if (viewer.isLagging) {
                        std::cout << "[Network Telemetry] Viewer recovered. TCP pipe cleared after dropping " 
                                  << viewer.consecutiveDrops << " frames.\n";
                        viewer.isLagging = false;
                        viewer.consecutiveDrops = 0;
                    }

                    res->cork([&]() {
                        res->write(HttpHeaders::MJPEG_CHUNK_PREFIX);
                        
                        char headerBuf[ServerConstants::STACK_BUF_SIZE];
                        auto result = std::to_chars(headerBuf, headerBuf + ServerConstants::STACK_BUF_SIZE, currentFrame->size());
                        res->write(std::string_view(headerBuf, result.ptr - headerBuf));
                        
                        res->write(HttpHeaders::MJPEG_CHUNK_SUFFIX);
                        res->write(std::string_view(reinterpret_cast<const char*>(currentFrame->data()), currentFrame->size()));
                    });
                } else {
                    if (!viewer.isLagging) {
                        std::cout << "[Network Telemetry] Warning: TCP stall detected! OS buffer backed up with " 
                                  << backpressure << " bytes. Dropping frames to maintain real-time latency...\n";
                        viewer.isLagging = true;
                    }
                    viewer.consecutiveDrops++;
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

    networkThread = std::thread([this, &loopPromise]() {
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
            if (activeViewers.size() >= ServerConstants::MAX_CLIENTS) {
                res->writeStatus("503 Service Unavailable")->end("Server Capacity Reached");
                return;
            }

            res->writeStatus("200 OK")
               ->writeHeader("Connection", "close")
               ->writeHeader("Cache-Control", "no-cache, private")
               ->writeHeader("Pragma", "no-cache")
               ->writeHeader("Content-Type", "multipart/x-mixed-replace; boundary=mjpegstream");

            activeViewers.push_back({res, 0, false});

            res->onAborted([this, res]() {
                auto it = std::find_if(activeViewers.begin(), activeViewers.end(),
                    [res](const ViewerState& v) { return v.res == res; });
                    
                if (it != activeViewers.end()) {
                    it->isClosed = true;
                }
            });
        });

        app.any("/*", [](auto *res, auto *) {
            res->writeStatus("404 Not Found")->end();
        });

        app.listen(port, [this](us_listen_socket_t *socket) {
            if (socket) {
                listenSocket = socket;
                std::cout << "[Network Core] Asynchronous uWebSockets engine listening on port " << port << '\n';

                constexpr int TIMER_FALLTHROUGH = 0;
                constexpr int TIMER_INTERVAL_MS = 15;

                auto *loop = reinterpret_cast<struct us_loop_t *>(uWS::Loop::get());
                us_timer_t *timer = us_create_timer(loop, TIMER_FALLTHROUGH, sizeof(MjpegServer*));
                
                *static_cast<MjpegServer**>(us_timer_ext(timer)) = this;
                us_timer_set(timer, MjpegServer::onTimer, TIMER_INTERVAL_MS, TIMER_INTERVAL_MS);
            } else {
                std::cerr << "[Network Core Error] Failed to bind to port " << port << '\n';
            }
        });

        loopPromise.set_value(); 
        app.run();
    });

    loopFuture.wait();
}