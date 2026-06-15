#pragma once
#include "disruptor.hpp"
#include "video_frame.hpp"

inline constexpr int64_t VIDEO_FRAME_BUFFER_CAPACITY = 128;

using VideoFrameBuffer =
    disruptor::Disruptor<VideoFrame, VIDEO_FRAME_BUFFER_CAPACITY, disruptor::BlockingWaitStrategy>;
