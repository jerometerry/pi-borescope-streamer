#pragma once
#include "disruptor.hpp"
#include "array.hpp"

inline constexpr int64_t ARRAY_BUFFER_CAPACITY = 128;

using ArrayBuffer = disruptor::Disruptor<
    Array, 
    ARRAY_BUFFER_CAPACITY, 
    disruptor::BlockingWaitStrategy
>;
