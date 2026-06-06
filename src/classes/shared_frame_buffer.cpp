#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
#include <vector>
#include "buffer_pool.hpp"
#include "shared_frame_buffer.hpp"

SharedFrameBuffer::SharedFrameBuffer(std::shared_ptr<BufferPool> bufferPool) : 
    bufferPool_(std::move(bufferPool)) {
}

void SharedFrameBuffer::push(std::span<const uint8_t> frame) {
    if (frame.empty()) { 
		return;
	}

    auto buffer = bufferPool_->acquire();
    buffer->assign(frame.begin(), frame.end());

    std::shared_ptr<const std::vector<uint8_t>> previousFrame;
    {
        std::scoped_lock lock(activeMutex_);
        frameId_++;
        previousFrame = std::move(frame_);
        frame_ = std::move(buffer);
    }
}

std::shared_ptr<const std::vector<uint8_t>> SharedFrameBuffer::getLatestFrame(uint32_t& outFrameId) const {
    std::scoped_lock lock(activeMutex_);
    outFrameId = frameId_;
    return frame_;
}
