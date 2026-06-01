#include <cstdint>
#include <mutex>
#include <vector>
#include "server_constants.hpp"
#include "shared_frame_pipeline.hpp"

 SharedFramePipeline::SharedFramePipeline() {
        currentFrame_.reserve(ServerConstants::FORTY_KILOBYTES);
        snapshotFrame_.reserve(ServerConstants::FORTY_KILOBYTES);
    }

void SharedFramePipeline::updateFrame(const std::vector<uint8_t>& frame) {
	if (frame.empty()) return;
	std::scoped_lock lock(mutex_);
	currentFrame_ = frame;
	frameId_++;

	if (captureSnapshotRequested_) {
		snapshotFrame_ = currentFrame_;
		captureSnapshotRequested_ = false;
	}
}

void SharedFramePipeline::requestSnapshot() {
	std::scoped_lock lock(mutex_);
	captureSnapshotRequested_ = true;
}

std::vector<uint8_t> SharedFramePipeline::getCurrentFrame(uint32_t& outFrameId) const {
	std::scoped_lock lock(mutex_);
	outFrameId = frameId_;
	return currentFrame_;
}

std::vector<uint8_t> SharedFramePipeline::getSnapshot() const {
	std::scoped_lock lock(mutex_);
	return snapshotFrame_;
}