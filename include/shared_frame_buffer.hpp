#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>
class BufferPool;

class SharedFrameBuffer : public std::enable_shared_from_this<SharedFrameBuffer> {
public:
    explicit SharedFrameBuffer(std::shared_ptr<BufferPool> bufferPool);
    void push(std::span<const uint8_t> frame);
    std::shared_ptr<const std::vector<uint8_t>> getLatestFrame(uint32_t& outFrameId) const;

private:
    std::shared_ptr<BufferPool> bufferPool_;

    mutable std::mutex activeMutex_;
    std::shared_ptr<const std::vector<uint8_t>> frame_;

    uint32_t frameId_{0};    
};