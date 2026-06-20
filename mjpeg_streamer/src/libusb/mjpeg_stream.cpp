#include "mjpeg_stream.hpp"
extern "C" {
#include "useeplus_protocol.h"
}

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "constants.hpp"
#include "endian_conversion.hpp"
#include "video_frame_fragment.hpp"
#include "video_frame_buffer.hpp"

MjpegStream::MjpegStream(VideoFrameBuffer& disruptor) : disruptor_(&disruptor) {
    inputBuffer_.reserve(Units::THIRTY_TWO_KILOBYTES);

    decoder_.context = this;
    decoder_.cb.on_video_frame_start = MjpegStream::onFrameStartCallback;
    decoder_.cb.on_video_frame_fragment = MjpegStream::onVideoPayloadCallback;
    decoder_.cb.on_video_frame_complete = MjpegStream::onFrameCompleteCallback;
    decoder_.cb.on_video_frame_incomplete = MjpegStream::onFrameIncompleteCallback;
}

void MjpegStream::send(std::span<const uint8_t> data) {
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

void MjpegStream::onFrameStartCallback(
    void* context, uint8_t frameId,
    uint8_t devNum) {  // NOLINT(bugprone-easily-swappable-parameters)
    auto* self = static_cast<MjpegStream*>(context);

    if (self->frameActive_) {
        self->disruptor_->publish(self->currentClaimSequence_);
    }

    if (self->lastFrameId_ != 0 || frameId != 0) {
        uint8_t expectedId = self->lastFrameId_ + 1;

        if (frameId != expectedId) {
            uint8_t framesLost = frameId - expectedId;
            self->hardwareDroppedFrames_ += framesLost;
        }
    }
    self->lastFrameId_ = frameId;

    self->currentClaimSequence_ = self->disruptor_->claim();
    VideoFrameFragment& slot = self->disruptor_->getBySequence(self->currentClaimSequence_);
    slot.clear();

    self->frameActive_ = true;
}

void MjpegStream::onVideoPayloadCallback(void* context, uint8_t* data, size_t len) {
    auto* self = static_cast<MjpegStream*>(context);
    if (self->frameActive_) {
        VideoFrameFragment& slot = self->disruptor_->getBySequence(self->currentClaimSequence_);
        slot.insertContent(std::span<const uint8_t>(data, len));
    }
}

void MjpegStream::onFrameCompleteCallback(void* context) {
    auto* self = static_cast<MjpegStream*>(context);

    if (self->frameActive_) {
        self->disruptor_->publish(self->currentClaimSequence_);
        self->frameActive_ = false;
    }
}

void MjpegStream::onFrameIncompleteCallback(void* context) {
    auto* self = static_cast<MjpegStream*>(context);

    if (self->frameActive_) {
        VideoFrameFragment& slot = self->disruptor_->getBySequence(self->currentClaimSequence_);
        slot.clear();
        self->disruptor_->publish(self->currentClaimSequence_);
        self->frameActive_ = false;
    }
}
