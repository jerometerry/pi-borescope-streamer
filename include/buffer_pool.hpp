#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "buffer.hpp"
#include "buffer_ptr.hpp"
#include "buffer_recycler.hpp"
#include "thread_safety.hpp"
#include "thread_safety_mutex.hpp"

/**
 * @brief BufferPool manages a collection of Buffers that are pre-allocated at startup to avoid
 * memory allocations on the hot paths. Buffers are wrapped with IntrusivePtr, and are automatically
 * returned to the pool if the pool size is less than the max size, otherwise the Buffer is deleted.
 */
class BufferPool final : public BufferRecycler, public std::enable_shared_from_this<BufferPool> {
   public:
    /**
     * @brief Configuration parameters for initializing the memory pool.
     */
    struct BufferPoolArgs {
        size_t maxPoolSize;
        size_t initialPoolSize;
        size_t bufferReserveSize;
    };

    explicit BufferPool(const BufferPoolArgs& args);

    static std::shared_ptr<BufferPool> create();

    static std::shared_ptr<BufferPool> create(const BufferPoolArgs& args);

    /**
     * @brief Retrieves an available Buffer from the pool.
     * @details If the pool is empty but under maxPoolSize_, a new Buffer is allocated.
     * The returned Buffer is wrapped in an IntrusivePtr; when its reference count drops to 0,
     * it will automatically return itself to this pool.
     *
     * @return A managed pointer to a zeroed-out, ready-to-use Buffer.
     */
    BufferPtr borrow();

    size_t getFreeBuffers() const;

    void recycle(Buffer* buffer) override;

   private:
    friend Buffer;

    void initialize();

    mutable Mutex poolMutex_;
    std::vector<std::unique_ptr<Buffer>> pool_ GUARDED_BY(poolMutex_);

    const size_t maxPoolSize_;
    const size_t initialPoolSize_;
    const size_t bufferReserveSize_;
};
