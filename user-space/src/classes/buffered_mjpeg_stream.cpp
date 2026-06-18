#include "buffered_mjpeg_stream.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "buffer.hpp"
#include "buffer_pool.hpp"
#include "buffer_ptr.hpp"
#include "constants.hpp"
#include "endian_conversion.hpp"
#include "intrusive_ptr.hpp"

BufferedMjpegStream::BufferedMjpegStream(std::shared_ptr<BufferPool> bufferPool,
                                         std::function<void(BufferPtr)> onFrameReady)
    : bufferPool_(std::move(bufferPool)), onFrameReady_(std::move(onFrameReady)) {
    inputBuffer_.reserve(Units::THIRTY_TWO_KILOBYTES);
    activeFrame_ = bufferPool_->borrow();

    decoder_.context = this;
    decoder_.cb.on_frame_start = BufferedMjpegStream::onFrameStartCallback;
    decoder_.cb.on_video_payload = BufferedMjpegStream::onVideoPayloadCallback;
    decoder_.cb.on_frame_complete = BufferedMjpegStream::onFrameCompleteCallback;
    decoder_.cb.on_frame_incomplete = BufferedMjpegStream::onFrameIncompleteCallback;
}

void BufferedMjpegStream::send(std::span<const uint8_t> data) {
    inputBuffer_.insert(inputBuffer_.end(), data.begin(), data.end());

    size_t available = inputBuffer_.size() - readOffset_;
    if (available == 0) {
        return;
    }

    size_t consumed = up_decode_bulk(&decoder_, inputBuffer_.data() + readOffset_, available);

    readOffset_ += consumed;

    if (readOffset_ == inputBuffer_.size()) {
        inputBuffer_.clear();
        readOffset_ = 0;
    } else if (readOffset_ > Units::FOUR_KILOBYTES) {
        inputBuffer_.erase(inputBuffer_.begin(), inputBuffer_.begin() + readOffset_);
        readOffset_ = 0;
    }
}

void BufferedMjpegStream::onFrameStartCallback(void* context, uint8_t frameId, uint8_t devNum) {
    auto* self = static_cast<BufferedMjpegStream*>(context);

    if (self->lastFrameId_ != 0 || frameId != 0) {
        uint8_t expectedId = self->lastFrameId_ + 1;
        if (frameId != expectedId) {
            self->hardwareDroppedFrames_ += (frameId - expectedId);
        }
    }
    self->lastFrameId_ = frameId;

    if (!self->activeFrame_) {
        self->activeFrame_ = self->bufferPool_->borrow();
    } else {
        self->activeFrame_->clear();
    }

    self->frameActive_ = true;
}

void BufferedMjpegStream::onVideoPayloadCallback(void* context, uint8_t* data, size_t len) {
    auto* self = static_cast<BufferedMjpegStream*>(context);

    if (self->frameActive_ && self->activeFrame_) {
        self->activeFrame_->insertContent(std::span<const uint8_t>(data, len));
    }
}

void BufferedMjpegStream::onFrameCompleteCallback(void* context) {
    auto* self = static_cast<BufferedMjpegStream*>(context);

    if (self->frameActive_ && self->onFrameReady_ && self->activeFrame_) {
        self->onFrameReady_(std::move(self->activeFrame_));
        self->frameActive_ = false;
        self->activeFrame_ = nullptr;
    }
}

void BufferedMjpegStream::onFrameIncompleteCallback(void* context) {
    auto* self = static_cast<BufferedMjpegStream*>(context);

    if (self->frameActive_ && self->activeFrame_) {
        self->activeFrame_->clear();
        self->frameActive_ = false;
    }
}
