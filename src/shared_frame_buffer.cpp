#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
#include <vector>
#include "constants.hpp"
#include "shared_frame_buffer.hpp"

std::shared_ptr<SharedFrameBuffer> SharedFrameBuffer::create() {
    auto instance = std::make_shared<SharedFrameBuffer>();
    instance->initialize();
    return instance;
}

void SharedFrameBuffer::push(std::span<const uint8_t> frame) {
    if (frame.empty()) { 
		return;
	}

    auto buffer = acquire();
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

size_t SharedFrameBuffer::getFreeBuffers() const {
    std::scoped_lock lock(poolMutex_);
    return bufferPool_.size();
}

std::shared_ptr<std::vector<uint8_t>> SharedFrameBuffer::acquire() {
    std::unique_ptr<std::vector<uint8_t>> buffer;
    {
        std::scoped_lock lock(poolMutex_);
        if (!bufferPool_.empty()) {
            buffer = std::move(bufferPool_.back());
            bufferPool_.pop_back();
        }
    }

    if (!buffer) {
        buffer = std::make_unique<std::vector<uint8_t>>();
        buffer->reserve(Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);
    }

    auto weakThis = weak_from_this();
    return {buffer.release(), [weakThis](std::vector<uint8_t>* ptr) {                           
        std::unique_ptr<std::vector<uint8_t>> wrapper(ptr);
        if (auto sharedThis = weakThis.lock()) {
            sharedThis->release(std::move(wrapper));
        }
    }};
}

void SharedFrameBuffer::release(std::unique_ptr<std::vector<uint8_t>> buffer) {
    std::scoped_lock lock(poolMutex_);
    if (bufferPool_.size() < SharedFrameBufferConfig::MAX_SHARED_FRAME_POOL_SIZE) {
        bufferPool_.push_back(std::move(buffer));
    }
}

void SharedFrameBuffer::initialize() {
    for (int i = 0; i < SharedFrameBufferConfig::INITIAL_SHARED_FRAME_POOL_SIZE; ++i) {
        auto buffer = std::make_unique<std::vector<uint8_t>>();
        buffer->reserve(Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);
        bufferPool_.push_back(std::move(buffer));
    }
}
