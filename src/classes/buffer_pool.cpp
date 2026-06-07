#include <cstddef>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>
#include "buffer_pool.hpp"
#include "constants.hpp"
#include "mjpeg_data_structures.hpp"

std::shared_ptr<BufferPool> BufferPool::create() {
    auto instance = std::make_shared<BufferPool>(PrivateConstructTag{});
    instance->initialize();
    return instance;
}

static void recycleFrameBridge(void* context, Mjpeg::Buffer* buffer) {
    auto* pool = static_cast<BufferPool*>(context);
    pool->returnToPool(buffer);
}

Mjpeg::Frame BufferPool::acquire() {
    std::unique_ptr<Mjpeg::Buffer> buffer;
    {
        std::scoped_lock lock(poolMutex_);
        if (!pool_.empty()) {
            buffer = std::move(pool_.back());
            pool_.pop_back();
        }
    }

    if (!buffer) {
        buffer = std::make_unique<Mjpeg::Buffer>();
        buffer->reserve(Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);
        buffer->setPoolContext(this);
        buffer->setReturnCallback(recycleFrameBridge);
    }

    return Mjpeg::Frame(buffer.release());
}

size_t BufferPool::getFreeBuffers() const {
    std::scoped_lock lock(poolMutex_);
    return pool_.size();
}

void BufferPool::initialize() {
    pool_.reserve(BufferPoolConfig::MAX_POOL_SIZE);
    
    for (int i = 0; i < BufferPoolConfig::INITIAL_POOL_SIZE; ++i) {

        auto buffer = std::make_unique<Mjpeg::Buffer>();
        buffer->reserve(Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);

        buffer->setPoolContext(this);
        buffer->setReturnCallback(recycleFrameBridge);

        pool_.push_back(std::move(buffer));
    }
}

void BufferPool::returnToPool(Mjpeg::Buffer* buffer) {
    if (!buffer) {
        return;
    }

    buffer->clear(); 

    std::scoped_lock lock(poolMutex_);
    if (pool_.size() < BufferPoolConfig::MAX_POOL_SIZE) {
        pool_.push_back(std::unique_ptr<Mjpeg::Buffer>(buffer));
    } else {
        std::unique_ptr<Mjpeg::Buffer> toDelete(buffer);
    }
}