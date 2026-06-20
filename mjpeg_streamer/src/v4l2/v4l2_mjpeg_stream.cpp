#include "v4l2_mjpeg_stream.hpp"

#include <cstdint>
#include <span>

#include "video_frame_buffer.hpp"
#include "video_frame_fragment.hpp"

V4l2MjpegStream::V4l2MjpegStream(VideoFrameBuffer& disruptor) : disruptor_(&disruptor) {}

void V4l2MjpegStream::send(std::span<const uint8_t> data) {
    if (data.empty()) return;

    currentClaimSequence_ = disruptor_->claim();
    VideoFrameFragment& slot = disruptor_->getBySequence(currentClaimSequence_);
    slot.clear();
    slot.insertContent(data);
    disruptor_->publish(currentClaimSequence_);
}
