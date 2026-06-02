#pragma once

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

class SharedFramePipeline;
namespace uWS { template <bool SSL> struct HttpResponse; }
struct us_listen_socket_t;
struct us_timer_t;

/**
 * @brief The server that streams the USB camera video to your web browser or video player.
 * @details MjpegServer bridges the physical USB camera and your network using the highly 
 * optimized uWebSockets epoll engine. It takes the raw video pictures coming from the 
 * hardware and packages them into an MJPEG stream that any standard web browser can display.
 */
class MjpegServer {
public:
    explicit MjpegServer(int port, const std::atomic<bool>& running, SharedFramePipeline& pipeline);
    ~MjpegServer();

    MjpegServer(const MjpegServer&) = delete;
    MjpegServer& operator=(const MjpegServer&) = delete;

    void start();

private:
    struct ViewerState {
        uWS::HttpResponse<false>* res;
        uint32_t lastSentFrameId;
        bool isClosed;
    };

    const int port;
    const std::atomic<bool>& running;
    SharedFramePipeline& pipeline;

    std::thread networkThread;
    us_listen_socket_t* listenSocket{nullptr};

    std::vector<ViewerState> activeViewers;
    uint32_t lastBroadcastedFrameId{0};

    static void onTimer(us_timer_t *t);
};