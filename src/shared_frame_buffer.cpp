#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
#include <vector>
#include "shared_frame_buffer.hpp"
#include "server_constants.hpp"

SharedFrameBuffer::SharedFrameBuffer() {
    for (int i = 0; i < ServerConstants::INITIAL_SHARED_FRAME_POOL_SIZE; ++i) {
        auto buffer = std::make_unique<std::vector<uint8_t>>();
        buffer->reserve(ServerConstants::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);
        freePool_.push_back(std::move(buffer));
    }
}

[[gnu::noinline]]
void SharedFrameBuffer::push(std::span<const uint8_t> frame) {
    if (frame.empty()) { 
		return;
	}

    auto buffer = checkoutBuffer();
    buffer->assign(frame.begin(), frame.end());

    std::shared_ptr<const std::vector<uint8_t>> oldFrame;
    {
        std::scoped_lock lock(activeMutex_);
        frameId_++;
        oldFrame = std::move(latestFrame_);
        latestFrame_ = std::move(buffer);
    }
}

std::shared_ptr<const std::vector<uint8_t>> SharedFrameBuffer::getLatestFrame(uint32_t& outFrameId) const {
    std::scoped_lock lock(activeMutex_);
    outFrameId = frameId_;
    return latestFrame_;
}

std::shared_ptr<std::vector<uint8_t>> SharedFrameBuffer::checkoutBuffer() {
    std::unique_ptr<std::vector<uint8_t>> buffer;
    {
        std::scoped_lock lock(poolMutex_);
        if (!freePool_.empty()) {
            buffer = std::move(freePool_.back());
            freePool_.pop_back();
        }
    }

    if (!buffer) {
        buffer = std::make_unique<std::vector<uint8_t>>();
        buffer->reserve(ServerConstants::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);
    }

    auto weakThis = weak_from_this();
    return {buffer.release(), [weakThis](std::vector<uint8_t>* ptr) {
        std::unique_ptr<std::vector<uint8_t>> wrapper(ptr);
        if (auto sharedThis = weakThis.lock()) {
            sharedThis->returnBuffer(std::move(wrapper));
        }
    }};
}

void SharedFrameBuffer::returnBuffer(std::unique_ptr<std::vector<uint8_t>> buffer) {
    std::scoped_lock lock(poolMutex_);
    if (freePool_.size() < ServerConstants::MAX_SHARED_FRAME_POOL_SIZE) {
        freePool_.push_back(std::move(buffer));
    }
}
