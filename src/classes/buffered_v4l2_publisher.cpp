#include "buffered_v4l2_publisher.hpp"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>
#include <span>
#include <string>

#include "buffer.hpp"
#include "buffer_ptr.hpp"
#include "v4l2.hpp"

BufferedV4l2Publisher::BufferedV4l2Publisher(const V4L2::Config& config)
    : v4l2_fd_(open(config.devicePath.c_str(), O_RDWR)) {
    if (v4l2_fd_ < 0) {
        std::cerr << "[V4L2 Core] Failed to open " << config.devicePath
                  << ". Is v4l2loopback loaded?\n";
        return;
    }

    struct v4l2_format vid_format = {};
    vid_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

    // Suppress C++ union warnings because we are interfacing with a C Linux kernel API
    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
    vid_format.fmt.pix.width = config.width;
    vid_format.fmt.pix.height = config.height;
    vid_format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    vid_format.fmt.pix.sizeimage = config.sizeImage;
    vid_format.fmt.pix.field = V4L2_FIELD_NONE;
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)

    if (ioctl(v4l2_fd_, VIDIOC_S_FMT, &vid_format) < 0) {
        std::cerr << "[V4L2 Core] Failed to set loopback video format.\n";
        close(v4l2_fd_);
        v4l2_fd_ = -1;
        return;
    }

    std::cout << "[V4L2 Core] Virtual video device initialized at " << config.width << "x"
              << config.height << " on " << config.devicePath << ".\n";
}

BufferedV4l2Publisher::~BufferedV4l2Publisher() {
    if (v4l2_fd_ >= 0) {
        close(v4l2_fd_);
    }
}

void BufferedV4l2Publisher::writeFrame(const BufferPtr& frame) {
    if (v4l2_fd_ >= 0) {
        ssize_t bytesWritten =
            write(v4l2_fd_, frame->getContentSlice().data(), frame->contentSize());
        if (bytesWritten < 0) {
            std::cerr << "[V4L2 Core] Warning: Failed to write frame to loopback device.\n";
        }
    }
}