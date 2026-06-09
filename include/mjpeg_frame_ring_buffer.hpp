#pragma once
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <vector>
#include "buffer_ptr.hpp"

class MjpegFrameRingBuffer {
public:
    explicit MjpegFrameRingBuffer(size_t size, const std::atomic<bool>& running);

    void push(BufferPtr frame);

    BufferPtr pop();

    /**
     * @brief Pokes the condition variable to wake up any blocked consumer threads 
     * during a system teardown.
     */
    void shutdown();

private:
    std::vector<BufferPtr> pool_;

    size_t head_ = 0;
    size_t tail_ = 0;
    size_t capacity_;

    std::mutex mtx_;
    std::condition_variable cv_;

    const std::atomic<bool>& running_;
};
