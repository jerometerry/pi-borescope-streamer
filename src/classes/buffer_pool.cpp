#include <cstddef>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>
#include "buffer_pool.hpp"
#include "constants.hpp"
#include "mjpeg_data_structures.hpp"

BufferPool::BufferPool(const BufferPoolArgs& args) 
    : maxPoolSize_(args.maxPoolSize), 
      initialPoolSize_(args.initialPoolSize), 
      bufferReserveSize_(args.bufferReserveSize) {
}

std::shared_ptr<BufferPool> BufferPool::create() {
    BufferPoolArgs args {
        BufferPoolConfig::MAX_POOL_SIZE,
        BufferPoolConfig::INITIAL_POOL_SIZE,
        Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES
    };
    return create(args);
}

std::shared_ptr<BufferPool> BufferPool::create(const BufferPoolArgs& args) {
    auto instance = std::make_shared<BufferPool>(args);
    instance->initialize();
    return instance;
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
        buffer->reserve(bufferReserveSize_);
        buffer->setPoolContext(this);
        buffer->setReturnCallback(recycleFrameBridge);
    }

    return Mjpeg::Frame(buffer.release());
}

size_t BufferPool::getFreeBuffers() const {
    std::scoped_lock lock(poolMutex_);
    return pool_.size();
}

void BufferPool::returnToPool(Mjpeg::Buffer* buffer) {
    if (!buffer) {
        return;
    }

    buffer->clear(); 

    std::scoped_lock lock(poolMutex_);
    if (pool_.size() < maxPoolSize_) {
        pool_.push_back(std::unique_ptr<Mjpeg::Buffer>(buffer));
    } else {
        std::unique_ptr<Mjpeg::Buffer> toDelete(buffer);
    }
}

void BufferPool::initialize() {
    pool_.reserve(maxPoolSize_);
    
    for (size_t i = 0; i < initialPoolSize_; ++i) {
        auto buffer = std::make_unique<Mjpeg::Buffer>();
        buffer->reserve(bufferReserveSize_);
        buffer->setPoolContext(this);
        buffer->setReturnCallback(recycleFrameBridge);
        pool_.push_back(std::move(buffer));
    }
}

void BufferPool::recycleFrameBridge(void* context, Mjpeg::Buffer* buffer) {
    auto* pool = static_cast<BufferPool*>(context);
    pool->returnToPool(buffer);
}