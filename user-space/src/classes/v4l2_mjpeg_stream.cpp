#include "v4l2_mjpeg_stream.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "constants.hpp"
#include "endian_conversion.hpp"
#include "video_frame.hpp"
#include "video_frame_buffer.hpp"

V4l2MjpegStream::V4l2MjpegStream(VideoFrameBuffer& disruptor) : disruptor_(&disruptor) {
}

void V4l2MjpegStream::send(std::span<const uint8_t> data) {
    if (data.empty())
        return;

    currentClaimSequence_ = disruptor_->claim();
    VideoFrame& slot = disruptor_->getBySequence(currentClaimSequence_);
    slot.clear();
    slot.insertContent(data);
    disruptor_->publish(currentClaimSequence_);
}
