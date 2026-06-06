#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>
#include "data_structures.hpp"

class BufferPool : public std::enable_shared_from_this<BufferPool> {
private:
    struct PrivateConstructTag {};

public:
    explicit BufferPool(PrivateConstructTag) {}
    
    static std::shared_ptr<BufferPool> create();
    
    USB::FramePtr acquire();

    void returnToPool(USB::PooledFrame* frame);
    
    size_t getFreeBuffers() const;

private:
    void initialize();
    void release(std::unique_ptr<std::vector<uint8_t>> buffer);
    
    mutable std::mutex poolMutex_;
    std::vector<std::unique_ptr<USB::PooledFrame>> pool_;
};