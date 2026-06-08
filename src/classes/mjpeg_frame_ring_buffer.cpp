#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>
#include "buffer.hpp"
#include "frame_ring_buffer.hpp"
#include "constants.hpp"

MjpegFrameRingBuffer::MjpegFrameRingBuffer(
	size_t size, 
	const std::atomic<bool>& running) : 
		capacity_(size + 1), running_(running) {
	pool_.resize(capacity_);
	for (size_t i = 0; i < capacity_; ++i) {
		pool_[i] = std::make_shared<Buffer>(BufferPoolConfig::BUFFER_PADDING);
	}
}

void MjpegFrameRingBuffer::push(const std::shared_ptr<Buffer>& frame) {
	std::scoped_lock<std::mutex> lock(mtx_);
	
	pool_[head_] = frame;
	head_ = (head_ + 1) % capacity_;
	
	if (head_ == tail_) {
		tail_ = (tail_ + 1) % capacity_;
	}
	
	cv_.notify_one();
}

std::shared_ptr<Buffer> MjpegFrameRingBuffer::pop() {
	std::unique_lock<std::mutex> lock(mtx_);
	
	cv_.wait(lock, [this]() { 
		return head_ != tail_ || !running_; 
	});

	if (!running_) {
		return nullptr;
	}

	auto frame = pool_[tail_];
	tail_ = (tail_ + 1) % capacity_;
	
	return frame;
}
