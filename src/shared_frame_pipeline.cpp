#include <cstdint>
#include <mutex>
#include <vector>
#include "server_constants.hpp"
#include "shared_frame_pipeline.hpp"

SharedFramePipeline::SharedFramePipeline() {
    for (int i = 0; i < 3; ++i) {
        auto buffer = std::make_shared<std::vector<uint8_t>>();
        buffer->reserve(ServerConstants::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);
        freePool_.push_back(buffer);
    }

    latestFrame_ = freePool_.back();
    freePool_.pop_back();
    
    snapshotFrame_ = std::make_shared<std::vector<uint8_t>>();
    std::const_pointer_cast<std::vector<uint8_t>>(snapshotFrame_)->reserve(ServerConstants::FORTY_KILOBYTES);
}

void SharedFramePipeline::updateFrame(const std::vector<uint8_t>& frame) {
    if (frame.empty()) return;

    std::shared_ptr<std::vector<uint8_t>> writeBuffer;
    
    {
        std::scoped_lock lock(poolMutex_);
        if (freePool_.empty()) {
            return;
        }
        writeBuffer = freePool_.back();
        freePool_.pop_back();
    }

    *writeBuffer = frame;
    
    std::shared_ptr<const std::vector<uint8_t>> oldActive;
    
    {
        std::scoped_lock lock(activeMutex_);
        frameId_++;
        oldActive = latestFrame_;
        latestFrame_ = writeBuffer;
        
        if (captureSnapshotRequested_) {
            *std::const_pointer_cast<std::vector<uint8_t>>(snapshotFrame_) = *writeBuffer;;
            captureSnapshotRequested_ = false;
        }
    }

    {
        std::scoped_lock lock(poolMutex_);
        freePool_.push_back(std::const_pointer_cast<std::vector<uint8_t>>(oldActive));
    }
}

void SharedFramePipeline::requestSnapshot() {
    std::scoped_lock lock(activeMutex_);
    captureSnapshotRequested_ = true;
}

std::shared_ptr<const std::vector<uint8_t>> SharedFramePipeline::getCurrentFrame(uint32_t& outFrameId) const {
    std::scoped_lock lock(activeMutex_);
    outFrameId = frameId_;
    return latestFrame_;
}

std::shared_ptr<const std::vector<uint8_t>> SharedFramePipeline::getSnapshot() const {
    std::scoped_lock lock(activeMutex_);
    return snapshotFrame_;
}