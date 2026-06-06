#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
#include <vector>
#include "buffer_pool.hpp"
#include "data_structures.hpp"
#include "shared_frame_buffer.hpp"
#include "mjpeg_data_structures.hpp"

void SharedFrameBuffer::push(Mjpeg::Frame frame) {
    if (!frame || frame->empty()) { 
        return;
    }

    Mjpeg::Frame previousFrame;
    {
        std::scoped_lock lock(activeMutex_);
        frameId_++;
        previousFrame = std::move(frame_);
 
        frame_ = std::move(frame);
    }
}

Mjpeg::Frame SharedFrameBuffer::getLatestFrame(uint32_t& outFrameId) const {
    std::scoped_lock lock(activeMutex_);
    outFrameId = frameId_;
    return frame_;
}
