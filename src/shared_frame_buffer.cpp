#include <cstdint>
#include <mutex>
#include <span>
#include <utility>
#include <vector>
#include "shared_frame_buffer.hpp"
#include "server_constants.hpp"

SharedFrameBuffer::SharedFrameBuffer() {
    for (int i = 0; i < 4; ++i) {
        auto buffer = std::make_shared<std::vector<uint8_t>>();
        buffer->reserve(ServerConstants::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);
        freePool_.push_back(buffer);
    }

    latestFrame_ = freePool_.back();
    freePool_.pop_back();
}

[[gnu::noinline]]
void SharedFrameBuffer::push(std::span<const uint8_t> frame) {
    if (frame.empty()) { 
		return;
	}

    auto newCanvas = checkoutBuffer();

    if (!newCanvas) {
        newCanvas = std::make_shared<std::vector<uint8_t>>();
        newCanvas->reserve(ServerConstants::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);
    }

    newCanvas->assign(frame.begin(), frame.end());

    std::shared_ptr<const std::vector<uint8_t>> oldActive;

    {
        std::scoped_lock lock(activeMutex_);
        frameId_++;
        oldActive = std::move(latestFrame_);
        latestFrame_ = std::move(newCanvas);
    }

    if (oldActive) {
        if (oldActive.use_count() == 1) {
            returnBuffer(std::const_pointer_cast<std::vector<uint8_t>>(std::move(oldActive)));
        }
    }
}

std::shared_ptr<const std::vector<uint8_t>> SharedFrameBuffer::getLatestFrame(uint32_t& outFrameId) const {
    std::scoped_lock lock(activeMutex_);
    outFrameId = frameId_;
    return latestFrame_;
}

std::shared_ptr<std::vector<uint8_t>> SharedFrameBuffer::checkoutBuffer() {
    std::scoped_lock lock(poolMutex_);
    if (freePool_.empty()) {
        return nullptr;
    }    
    auto buf = freePool_.back();
    freePool_.pop_back();
    return buf;
}

void SharedFrameBuffer::returnBuffer(std::shared_ptr<std::vector<uint8_t>> buffer) {
    if (!buffer) {
        return;
    }
    std::scoped_lock lock(poolMutex_);
    freePool_.push_back(std::move(buffer));
}
