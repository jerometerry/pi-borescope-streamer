#pragma once
#include "video_frame.hpp"

namespace V4L2 {
struct Config;
}

/**
 * @brief Bridges raw MJPEG frames into the Linux Kernel's Video4Linux subsystem.
 */
class V4l2Publisher {
   public:
    /**
     * @brief Construct a new V4l2Publisher object
     *
     * @param config
     */
    explicit V4l2Publisher(const V4L2::Config& config);

    /**
     * @brief Destroy the V4l2Publisher object
     */
    ~V4l2Publisher();

    /**
     * @brief Writes a completed JPEG frame directly to the virtual video node.
     */
    void writeFrame(const VideoFrame& frame);

   private:
    int v4l2_fd_{-1};
};
