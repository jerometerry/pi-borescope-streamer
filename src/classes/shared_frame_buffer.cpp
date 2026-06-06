#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
#include <vector>
#include "buffer_pool.hpp"
#include "data_structures.hpp"
#include "shared_frame_buffer.hpp"

SharedFrameBuffer::SharedFrameBuffer(std::shared_ptr<BufferPool> bufferPool) : 
    bufferPool_(std::move(bufferPool)) {
}

void SharedFrameBuffer::push(std::span<const uint8_t> frame) {
    if (frame.empty()) { 
        return;
    }

    auto buffer = bufferPool_->acquire();

    buffer->data().assign(frame.begin(), frame.end());

    USB::FramePtr previousFrame;
    {
        std::scoped_lock lock(activeMutex_);
        frameId_++;
        previousFrame = std::move(frame_);
        frame_ = std::move(buffer);
    }
}

USB::FramePtr SharedFrameBuffer::getLatestFrame(uint32_t& outFrameId) const {
    std::scoped_lock lock(activeMutex_);
    outFrameId = frameId_;
    return frame_;
}
