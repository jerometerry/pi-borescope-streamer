#pragma once
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <vector>

class SharedFramePipeline {
public:
    SharedFramePipeline();

    void updateFrame(std::shared_ptr<std::vector<uint8_t>> newFrame);

    std::shared_ptr<std::vector<uint8_t>> checkoutBuffer();
    void returnBuffer(std::shared_ptr<std::vector<uint8_t>> buffer);

    void requestSnapshot();

    std::shared_ptr<const std::vector<uint8_t>> getCurrentFrame(uint32_t& outFrameId) const;
    std::shared_ptr<const std::vector<uint8_t>> getSnapshot() const;
    
private:
    mutable std::mutex poolMutex_;
    mutable std::mutex activeMutex_;

    std::vector<std::shared_ptr<std::vector<uint8_t>>> freePool_;
    std::shared_ptr<const std::vector<uint8_t>> latestFrame_;
    std::shared_ptr<const std::vector<uint8_t>> snapshotFrame_;
    
    uint32_t frameId_{0};
    bool captureSnapshotRequested_{false};
    mutable bool initialSnapshotCaptured_{false};
};