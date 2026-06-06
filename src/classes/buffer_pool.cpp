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

static void recycleFrameBridge(void* context, Mjpeg::Buffer* frame) {
    auto* pool = static_cast<BufferPool*>(context);
    pool->returnToPool(frame);
}

Mjpeg::Frame BufferPool::acquire() {
    std::unique_ptr<Mjpeg::Buffer> frame;
    {
        std::scoped_lock lock(poolMutex_);
        if (!pool_.empty()) {
            frame = std::move(pool_.back());
            pool_.pop_back();
        }
    }

    if (!frame) {
        frame = std::make_unique<Mjpeg::Buffer>();
        frame->data().reserve(Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);
        frame->poolContext = this;
        frame->returnCallback = recycleFrameBridge;
    }

    return Mjpeg::Frame(frame.release());
}

size_t BufferPool::getFreeBuffers() const {
    std::scoped_lock lock(poolMutex_);
    return pool_.size();
}

void BufferPool::initialize() {
    pool_.reserve(BufferPoolConfig::MAX_POOL_SIZE);
    
    for (int i = 0; i < BufferPoolConfig::INITIAL_POOL_SIZE; ++i) {

        auto frame = std::make_unique<Mjpeg::Buffer>();
        frame->data().reserve(Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);

        frame->poolContext = this;
        frame->returnCallback = recycleFrameBridge;

        pool_.push_back(std::move(frame));
    }
}

void BufferPool::returnToPool(Mjpeg::Buffer* frame) {
    if (!frame) return;

    frame->clear(); 

    std::scoped_lock lock(poolMutex_);
    if (pool_.size() < BufferPoolConfig::MAX_POOL_SIZE) {
        pool_.push_back(std::unique_ptr<Mjpeg::Buffer>(frame));
    } else {
        std::unique_ptr<Mjpeg::Buffer> toDelete(frame);
    }
}