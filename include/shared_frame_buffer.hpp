#pragma once
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

class SharedFrameBuffer : public std::enable_shared_from_this<SharedFrameBuffer> {
public:
    SharedFrameBuffer();

    void push(std::span<const uint8_t> frame);

    std::shared_ptr<const std::vector<uint8_t>> getLatestFrame(uint32_t& outFrameId) const;

    size_t getFreePoolSize() const {
        std::scoped_lock lock(poolMutex_);
        return freePool_.size();
    }

private:
    std::shared_ptr<std::vector<uint8_t>> checkoutBuffer();
    void returnBuffer(std::unique_ptr<std::vector<uint8_t>> buffer);

    mutable std::mutex poolMutex_;
    std::vector<std::unique_ptr<std::vector<uint8_t>>> freePool_;

    mutable std::mutex activeMutex_;
    std::shared_ptr<const std::vector<uint8_t>> latestFrame_;
    uint32_t frameId_{0};
};