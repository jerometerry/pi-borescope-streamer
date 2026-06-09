#include "mjpeg_frame_ring_buffer.hpp"
#include <utility>

MjpegFrameRingBuffer::MjpegFrameRingBuffer(
    size_t size, 
    const std::atomic<bool>& running) : 
        capacity_(size + 1), running_(running) {

    pool_.resize(capacity_); 
}

void MjpegFrameRingBuffer::push(BufferPtr frame) {
    if (!frame || frame->empty()) {
        return;
    }

    std::scoped_lock<std::mutex> lock(mtx_);
    pool_[head_] = std::move(frame);
    head_ = (head_ + 1) % capacity_;
    
    // Drop-Oldest Backpressure Policy:
    // If the producer catches up to the consumer, advance the tail.
    // This orphans the oldest unread frame, allowing the IntrusivePtr 
    // to safely recycle it the next time the head overwrites that slot.
    if (head_ == tail_) {
        tail_ = (tail_ + 1) % capacity_;
    }
    
    cv_.notify_one();
}

BufferPtr MjpegFrameRingBuffer::pop() {
    std::unique_lock<std::mutex> lock(mtx_);
    
    cv_.wait(lock, [this]() { 
        return head_ != tail_ || !running_.load(std::memory_order_acquire); 
    });

    // If the system is shutting down and the queue is completely drained, exit safely
    if (!running_.load(std::memory_order_acquire) && head_ == tail_) {
        return {}; 
    }

    // Destructive Read: 
    // Moving the pointer transfers ownership to the local variable and 
    // leaves pool_[tail_] as a nullptr. This ensures the buffer recycles 
    // the moment the network thread finishes broadcasting it.
    BufferPtr frame = std::move(pool_[tail_]);
    tail_ = (tail_ + 1) % capacity_;
    
    return frame;
}

void MjpegFrameRingBuffer::shutdown() {
    // Wake up all sleeping consumers so they evaluate the running_ flag
    cv_.notify_all();
}
