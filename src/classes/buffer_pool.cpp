#include <cstddef>
#include <memory>
#include <utility>
#include <vector>
#include "buffer.hpp"
#include "buffer_pool.hpp"
#include "constants.hpp"
#include "frame.hpp"
#include "thread_safety_mutex.hpp"

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

Frame BufferPool::acquire() {
    std::unique_ptr<Buffer> buffer;

    {
        MutexLock lock(poolMutex_);
        if (!pool_.empty()) {
            buffer = std::move(pool_.back());
            pool_.pop_back();
        }
    }

    if (!buffer) {
        buffer = Buffer::unique();
        buffer->reserve(bufferReserveSize_);
        buffer->setPoolContext(this);
        buffer->setReturnCallback(recycleFrameBridge);
    }

    return Frame(buffer.release());
}

size_t BufferPool::getFreeBuffers() const {
    MutexLock lock(poolMutex_);
    return pool_.size();
}

void BufferPool::returnToPool(Buffer* buffer) {
    if (!buffer) {
        return;
    }

    buffer->clear(); 

    MutexLock lock(poolMutex_);
    if (pool_.size() < maxPoolSize_) {
        pool_.push_back(std::unique_ptr<Buffer>(buffer));
    } else {
        // Delay destruction of the previous Buffer until after poolMutex_ lock is released. 
        // Prevents a potential deadlock, if this is the only remaining reference.
        std::unique_ptr<Buffer> toDelete(buffer);
        // Suppress cppcheck unusedVariable warning
        (void)toDelete;
    }
}

void BufferPool::initialize() {
    MutexLock lock(poolMutex_);
    pool_.reserve(maxPoolSize_);
    
    for (size_t i = 0; i < initialPoolSize_; ++i) {
        auto buffer = Buffer::unique();
        buffer->reserve(bufferReserveSize_);
        buffer->setPoolContext(this);
        buffer->setReturnCallback(recycleFrameBridge);
        pool_.push_back(std::move(buffer));
    }
}

void BufferPool::recycleFrameBridge(void* context, Buffer* buffer) {
    auto* pool = static_cast<BufferPool*>(context);
    pool->returnToPool(buffer);
}
