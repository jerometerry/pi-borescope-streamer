#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>
#include "server_constants.hpp"
#include "shared_frame_pipeline.hpp"

SharedFramePipeline::SharedFramePipeline() {
    for (int i = 0; i < 4; ++i) {
        auto buffer = std::make_shared<std::vector<uint8_t>>();
        buffer->reserve(ServerConstants::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);
        freePool_.push_back(buffer);
    }

    latestFrame_ = freePool_.back();
    freePool_.pop_back();

    snapshotFrame_ = freePool_.back();
    freePool_.pop_back();
}

std::shared_ptr<std::vector<uint8_t>> SharedFramePipeline::checkoutBuffer() {
    std::scoped_lock lock(poolMutex_);
    if (freePool_.empty()) {
        return nullptr;
    }    
    auto buf = freePool_.back();
    freePool_.pop_back();
    return buf;
}

void SharedFramePipeline::returnBuffer(std::shared_ptr<std::vector<uint8_t>> buffer) {
    if (!buffer) {
        return;
    }
    std::scoped_lock lock(poolMutex_);
    freePool_.push_back(std::move(buffer));
}

void SharedFramePipeline::updateFrame(std::shared_ptr<std::vector<uint8_t>> newFrame) {
    if (!newFrame || newFrame->empty()) {
        returnBuffer(std::move(newFrame));
        return;
    }

    std::shared_ptr<const std::vector<uint8_t>> oldActive;
    std::shared_ptr<std::vector<uint8_t>> oldSnapshotToReturn;

    {
        std::scoped_lock lock(activeMutex_);
        frameId_++;
        
        oldActive = std::move(latestFrame_);
        latestFrame_ = std::move(newFrame);

        if (captureSnapshotRequested_) {
            auto newSnapshotBuffer = checkoutBuffer();
            
            if (newSnapshotBuffer) {
                newSnapshotBuffer->assign(latestFrame_->begin(), latestFrame_->end());

                oldSnapshotToReturn = std::move(snapshotFrame_);
                snapshotFrame_ = std::move(newSnapshotBuffer);
            }
            
            initialSnapshotCaptured_ = true;
            captureSnapshotRequested_ = false;
        }
    }

    if (oldActive) {
        returnBuffer(std::const_pointer_cast<std::vector<uint8_t>>(std::move(oldActive)));
    }
    
    if (oldSnapshotToReturn) {
        returnBuffer(std::move(oldSnapshotToReturn));
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

    if (!initialSnapshotCaptured_ && latestFrame_ && !latestFrame_->empty()) {
        snapshotFrame_->assign(latestFrame_->begin(), latestFrame_->end());
        initialSnapshotCaptured_ = true;
    }

    return snapshotFrame_;
}