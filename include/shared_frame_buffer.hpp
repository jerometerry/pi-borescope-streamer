#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include "data_structures.hpp"
class BufferPool;

namespace USB {
    class FramePtr;
}

class SharedFrameBuffer : public std::enable_shared_from_this<SharedFrameBuffer> {
public:
    explicit SharedFrameBuffer(std::shared_ptr<BufferPool> bufferPool);
    void push(USB::FramePtr frame);
    USB::FramePtr getLatestFrame(uint32_t& outFrameId) const;

private:
    std::shared_ptr<BufferPool> bufferPool_;

    mutable std::mutex activeMutex_;
    USB::FramePtr frame_;

    uint32_t frameId_{0};    
};