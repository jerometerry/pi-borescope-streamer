#pragma once
#include "disruptor.hpp"
#include "frame.hpp"

inline constexpr int64_t FRAME_DISRUPTOR_CAPACITY = 128;

using FrameDisruptor = disruptor::Disruptor<
    Frame, 
    FRAME_DISRUPTOR_CAPACITY, 
    disruptor::BlockingWaitStrategy
>;
