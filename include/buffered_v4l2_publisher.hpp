#pragma once
#include "buffer_ptr.hpp"

namespace V4L2 {
struct Config;
}

/**
 * @brief Bridges raw MJPEG frames into the Linux Kernel's Video4Linux subsystem.
 */
class BufferedV4l2Publisher {
   public:
    /**
     * @brief Construct a new V4l2Publisher object
     *
     * @param config
     */
    explicit BufferedV4l2Publisher(const V4L2::Config& config);

    /**
     * @brief Destroy the V4l2Publisher object
     */
    ~BufferedV4l2Publisher();

    /**
     * @brief Writes a completed JPEG frame directly to the virtual video node.
     */
    void writeFrame(const BufferPtr& frame);

   private:
    int v4l2_fd_{-1};
};