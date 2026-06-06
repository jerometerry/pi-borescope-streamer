#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

class SharedFrameBuffer : public std::enable_shared_from_this<SharedFrameBuffer> {
public:
    SharedFrameBuffer() = default;
    static std::shared_ptr<SharedFrameBuffer> create();
    void push(std::span<const uint8_t> frame);
    std::shared_ptr<const std::vector<uint8_t>> getLatestFrame(uint32_t& outFrameId) const;
    size_t getFreeBuffers() const;

private:
    void initialize();
    std::shared_ptr<std::vector<uint8_t>> acquire();
    void release(std::unique_ptr<std::vector<uint8_t>> buffer);
    mutable std::mutex poolMutex_;
    std::vector<std::unique_ptr<std::vector<uint8_t>>> bufferPool_;
    mutable std::mutex activeMutex_;
    std::shared_ptr<const std::vector<uint8_t>> frame_;
    uint32_t frameId_{0};
};