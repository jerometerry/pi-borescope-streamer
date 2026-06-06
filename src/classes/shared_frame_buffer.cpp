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

void SharedFrameBuffer::push(USB::FramePtr frame) {
    if (!frame || frame->empty()) { 
        return;
    }

    USB::FramePtr previousFrame;
    {
        std::scoped_lock lock(activeMutex_);
        frameId_++;
        previousFrame = std::move(frame_);
 
        frame_ = std::move(frame);
    }
}

USB::FramePtr SharedFrameBuffer::getLatestFrame(uint32_t& outFrameId) const {
    std::scoped_lock lock(activeMutex_);
    outFrameId = frameId_;
    return frame_;
}
