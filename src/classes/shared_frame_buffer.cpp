#include <cstdint>
#include <utility>

#include "buffer.hpp"
#include "frame.hpp"
#include "shared_frame_buffer.hpp"
#include "thread_safety_mutex.hpp"

void SharedFrameBuffer::push(Frame frame) {
    if (!frame || frame->empty()) { 
        return;
    }

    Frame previousFrame;

    {
        MutexLock lock(activeMutex_);
        frameId_++;

        // Delay destruction of the previous frame until after activeMutex_ lock is released. 
        // Prevents a potential deadlock, if this is the only remaining reference.
        previousFrame = std::move(frame_);
 
        frame_ = std::move(frame);
    }
}

Frame SharedFrameBuffer::getLatestFrame(uint32_t& outFrameId) const {
    MutexLock lock(activeMutex_);
    outFrameId = frameId_;
    return frame_;
}
