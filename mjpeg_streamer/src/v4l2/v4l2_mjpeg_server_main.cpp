/**
 * @file v4l2_mjpeg_server_main.cpp
 * @brief The main entry point for the live video streaming application.
 * @details This is the primary program you run to actually use your camera.
 * When executed, it automatically finds the camera on the USB bus, launches the
 * video decoding engine, spins up the network broadcaster, and starts serving
 * the live video feed to any web browser that connects to the Raspberry Pi's IP address.
 *
 * By default, it broadcasts on port 8080, but you can override this by passing
 * a different port number when you launch the program from the terminal.
 */

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "constants.hpp"
#include "mjpeg_server.hpp"
#include "v4l2_mjpeg_stream.hpp"
#include "video_frame_buffer.hpp"

namespace {
constexpr int DEFAULT_PORT = 8080;
constexpr std::string DEFAULT_DEVICE_PATH = "/dev/video0";

std::atomic<bool> running{true};
using FrameHandler = std::function<bool(std::span<const uint8_t>)>;
}  // namespace

class V4l2Camera {
    struct MmapBuffer {
        void* start;
        size_t length;
    };

    int fd_;
    std::vector<MmapBuffer> buffers_;
    FrameHandler frame_handler_;

   public:
    V4l2Camera(const std::string& device_path, FrameHandler frame_handler)
        : fd_(open(device_path.c_str(), O_RDWR | O_NONBLOCK, 0)),
          frame_handler_(std::move(frame_handler)) {
        if (fd_ < 0) {
            throw std::runtime_error("Cannot open " + device_path);
        }

        // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
        struct v4l2_format fmt = {};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
        fmt.fmt.pix.width = 640;
        fmt.fmt.pix.height = 480;
        if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
            throw std::runtime_error("Failed to set video format");
        }

        struct v4l2_requestbuffers req = {};
        req.count = 4;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd_, VIDIOC_REQBUFS, &req) < 0) {
            throw std::runtime_error("Failed to request buffers");
        }

        buffers_.resize(req.count);
        for (unsigned int i = 0; i < req.count; ++i) {
            struct v4l2_buffer buf = {};
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;
            ioctl(fd_, VIDIOC_QUERYBUF, &buf);

            buffers_[i].length = buf.length;
            buffers_[i].start =
                mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, buf.m.offset);

            ioctl(fd_, VIDIOC_QBUF, &buf);
        }
        // NOLINTEND(cppcoreguidelines-pro-type-union-access)

        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
            throw std::runtime_error("Failed to start streaming");
        }
    }

    ~V4l2Camera() {
        if (fd_ >= 0) {
            int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            ioctl(fd_, VIDIOC_STREAMOFF, &type);

            for (auto& buf : buffers_) {
                munmap(buf.start, buf.length);
            }
            close(fd_);
        }
    }

    void poll_frames() {
        while (running.load(std::memory_order_relaxed)) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(fd_, &fds);
            struct timeval tv = {1, 0};

            int r = select(fd_ + 1, &fds, nullptr, nullptr, &tv);

            if (r > 0) {
                struct v4l2_buffer buf = {};
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;

                if (ioctl(fd_, VIDIOC_DQBUF, &buf) == 0) {
                    if (!(buf.flags & V4L2_BUF_FLAG_ERROR)) {
                        uint8_t* payload_start = static_cast<uint8_t*>(buffers_[buf.index].start);
                        std::span<const uint8_t> payload(payload_start, buf.bytesused);

                        if (frame_handler_) {
                            frame_handler_(payload);
                        }
                    }
                    ioctl(fd_, VIDIOC_QBUF, &buf);
                }
            }
        }
    }
};

void signalHandler(int) {
    running.store(false, std::memory_order_release);
}

int main(int argc, const char* argv[]) {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGPIPE, SIG_IGN);

    try {
        int port = DEFAULT_PORT;
        std::string devicePath = DEFAULT_DEVICE_PATH;

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];

            if (arg == "-h" || arg == "--help") {
                std::print(
                    "Usage: {} [options]\n\n"
                    "Options:\n"
                    "  -h, --help    Show this help message and exit\n"
                    "  --dev <path>  Specify the video device path (default: {})\n"
                    "  --port <num>  Specify the server port number (default: {})\n",
                    argv[0], DEFAULT_DEVICE_PATH, DEFAULT_PORT);
                return 0;
            } else if (arg == "--dev" && i + 1 < argc) {
                devicePath = argv[++i];
            } else if (arg == "--port" && i + 1 < argc) {
                unsigned long val = std::stoul(argv[++i]);
                if (val > 65535) {
                    throw std::out_of_range(std::format("Invalid port: {}", val));
                }
                port = static_cast<int>(val);
            } else {
                throw std::invalid_argument(
                    std::format("Unknown argument: {}\nUse --help for usage details.", arg));
            }
        }

        VideoFrameBuffer ringBuffer;
        ringBuffer.preAllocate(Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);
        V4l2MjpegStream stream(ringBuffer);

        FrameHandler handler = [&stream](std::span<const uint8_t> payload) -> bool {
            if (!running.load(std::memory_order_relaxed)) {
                return false;
            }
            if (!payload.empty()) {
                stream.send(payload);
            }
            return true;
        };

        V4l2Camera v4l2_camera(devicePath, handler);
        MjpegServer server(port, running, ringBuffer);

        std::cout << "[Server Core] Starting asynchronous capture and network worker engines...\n";

        std::thread camera_thread(&V4l2Camera::poll_frames, &v4l2_camera);
        server.start();

        std::cout << "[Server Core] System fully operational. Awaiting network events.\n";

        while (running.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "[Server Core] Shutdown signal received. Stopping worker lanes...\n";

        if (camera_thread.joinable()) {
            camera_thread.join();
        }

    } catch (const std::exception& e) {
        std::cerr << "[Fatal] Unhandled exception in application core: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "[System Termination] All resources returned. Server exited cleanly.\n";
    return EXIT_SUCCESS;
}
