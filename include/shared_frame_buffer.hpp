#pragma once
#include <cstdint>
#include <memory>

#include "buffer_ptr.hpp"
#include "thread_safety.hpp"
#include "thread_safety_mutex.hpp"

class BufferPool;

class SharedFrameBuffer : public std::enable_shared_from_this<SharedFrameBuffer> {
public:
    SharedFrameBuffer() = default;
    void push(BufferPtr frame);
    BufferPtr getLatestFrame(uint32_t& outFrameId) const;

private:
    mutable Mutex activeMutex_;

    BufferPtr frame_ GUARDED_BY(activeMutex_);
    uint32_t frameId_ GUARDED_BY(activeMutex_){0};
};