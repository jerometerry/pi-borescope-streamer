#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

#include "buffer.hpp"
#include "buffer_ptr.hpp"
#include "thread_safety.hpp"
#include "thread_safety_mutex.hpp"

class MjpegFrameRingBuffer {
public:
    explicit MjpegFrameRingBuffer(size_t size, const std::atomic<bool>& running);
    void push(const std::shared_ptr<Buffer>& frame);
    std::shared_ptr<Buffer> pop();

private:
    std::vector<std::shared_ptr<Buffer>> pool_;
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t capacity_;
    std::mutex mtx_;
    std::condition_variable cv_;

    const std::atomic<bool>& running_;
};
