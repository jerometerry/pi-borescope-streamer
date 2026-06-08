#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "buffer.hpp"
#include "buffer_ptr.hpp"
#include "thread_safety.hpp"
#include "thread_safety_mutex.hpp"

/**
 * @brief BufferPool manages a collection of Buffers that are pre-allocated at startup to avoid memory allocations on 
 * the hot paths. Buffers are wrapped with IntrusivePtr, and are automatically returned to the pool if the pool size 
 * is less than the max size, otherwise the Buffer is deleted.
 */
class BufferPool : public std::enable_shared_from_this<BufferPool> {
public:
    struct BufferPoolArgs {
        size_t maxPoolSize;
        size_t initialPoolSize;
        size_t bufferReserveSize;
    };

    explicit BufferPool(const BufferPoolArgs& args);
    
    static std::shared_ptr<BufferPool> create();

    static std::shared_ptr<BufferPool> create(const BufferPoolArgs& args);
    
    BufferPtr borrow();

    size_t getFreeBuffers() const;

private:
    friend Buffer;  

    void recycle(Buffer* buffer);

    void initialize();
    static void recycleFrameBridge(void* context, Buffer* buffer);
    
    mutable Mutex poolMutex_;
    std::vector<std::unique_ptr<Buffer>> pool_ GUARDED_BY(poolMutex_);

    const size_t maxPoolSize_;
    const size_t initialPoolSize_;
    const size_t bufferReserveSize_;
};