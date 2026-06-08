#include <cstdint>
#include <utility>

#include "buffer.hpp"
#include "buffer_ptr.hpp"
#include "intrusive_ptr.hpp"
#include "mjpeg_frame_queue.hpp"
#include "thread_safety_mutex.hpp"

void MjpegFrameQueue::push(BufferPtr frame) {
    if (!frame || frame->empty()) { 
        return;
    }

    BufferPtr previousFrame;

    {
        MutexLock lock(activeMutex_);
        frameId_++;

        // Delay destruction of the previous frame until after activeMutex_ lock is released. 
        // Prevents a potential deadlock, if this is the only remaining reference.
        previousFrame = std::move(frame_);
 
        frame_ = std::move(frame);
    }
}

BufferPtr MjpegFrameQueue::pop(uint32_t& outFrameId) const {
    MutexLock lock(activeMutex_);
    outFrameId = frameId_;
    return frame_;
}
