#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "mjpeg_data_structures.hpp"
#include "thread_safety.hpp"
#include "thread_safety_mutex.hpp"

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
    
    Mjpeg::Frame acquire();

    void returnToPool(Mjpeg::Buffer* buffer);
    
    size_t getFreeBuffers() const;

private:
    void initialize();
    static void recycleFrameBridge(void* context, Mjpeg::Buffer* buffer);
    
    mutable Mutex poolMutex_;
    std::vector<std::unique_ptr<Mjpeg::Buffer>> pool_ GUARDED_BY(poolMutex_);

    const size_t maxPoolSize_;
    const size_t initialPoolSize_;
    const size_t bufferReserveSize_;
};