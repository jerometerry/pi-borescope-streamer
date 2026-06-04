#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

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
    using FrameSource = std::function<std::shared_ptr<const std::vector<uint8_t>>(uint32_t&)>;

    explicit MjpegServer(int port, const std::atomic<bool>& running, FrameSource frameSource);
    ~MjpegServer();

    MjpegServer(const MjpegServer&) = delete;
    MjpegServer& operator=(const MjpegServer&) = delete;

    void start();

private:
    struct ViewerState {
        uWS::HttpResponse<false>* res{};
        uint32_t lastSentFrameId{0};
        bool isClosed{false};

        bool isLagging{false};
        uint32_t lagStartFrameId{0};
    };

    const int port_;
    const std::atomic<bool>& running_;
    FrameSource frameSource_;

    std::thread networkThread_;
    us_listen_socket_t* listenSocket_{nullptr};

    std::vector<ViewerState> activeViewers_;
    uint32_t lastBroadcastedFrameId_{0};

    static void onTimer(us_timer_t *t);
};