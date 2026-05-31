#pragma once
#include <vector>
#include <mutex>
#include <cstdint>

class SharedFramePipeline {
public:
	SharedFramePipeline();

    void updateFrame(const std::vector<uint8_t>& frame);

    void requestSnapshot();

    std::vector<uint8_t> getCurrentFrame(uint32_t& outFrameId) const;

    std::vector<uint8_t> getSnapshot() const;
	
private:
    mutable std::mutex mutex_;
    std::vector<uint8_t> currentFrame_;
    std::vector<uint8_t> snapshotFrame_;
    uint32_t frameId_{0};
    bool captureSnapshotRequested_{false};
};
