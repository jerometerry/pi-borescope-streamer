#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
#include <vector>
#include "buffer_pool.hpp"
#include "constants.hpp"

std::shared_ptr<BufferPool> BufferPool::create() {
    auto instance = std::make_shared<BufferPool>();
    instance->initialize();
    return instance;
}

size_t BufferPool::getFreeBuffers() const {
    std::scoped_lock lock(poolMutex_);
    return pool_.size();
}

std::shared_ptr<std::vector<uint8_t>> BufferPool::acquire() {
    std::unique_ptr<std::vector<uint8_t>> buffer;
    {
        std::scoped_lock lock(poolMutex_);
        if (!pool_.empty()) {
            buffer = std::move(pool_.back());
            pool_.pop_back();
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

void BufferPool::release(std::unique_ptr<std::vector<uint8_t>> buffer) {
    std::scoped_lock lock(poolMutex_);
    if (pool_.size() < SharedFrameBufferConfig::MAX_SHARED_FRAME_POOL_SIZE) {
        pool_.push_back(std::move(buffer));
    }
}

void BufferPool::initialize() {
    for (int i = 0; i < SharedFrameBufferConfig::INITIAL_SHARED_FRAME_POOL_SIZE; ++i) {
        auto buffer = std::make_unique<std::vector<uint8_t>>();
        buffer->reserve(Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);
        pool_.push_back(std::move(buffer));
    }
}
