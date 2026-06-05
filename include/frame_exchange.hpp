#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

class FrameExchange {
public:
    FrameExchange();

    void publishFrame(std::span<const uint8_t> frameData);

    std::shared_ptr<const std::vector<uint8_t>> getLatestFrame(uint32_t& outFrameId) const;

private:
    std::shared_ptr<std::vector<uint8_t>> checkoutBuffer();
    void returnBuffer(std::shared_ptr<std::vector<uint8_t>> buffer);

    mutable std::mutex poolMutex_;
    std::vector<std::shared_ptr<std::vector<uint8_t>>> freePool_;

    mutable std::mutex activeMutex_;
    std::shared_ptr<const std::vector<uint8_t>> latestFrame_;
    uint32_t frameId_{0};
};