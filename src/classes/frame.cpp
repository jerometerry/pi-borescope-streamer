#include <atomic>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>
#include "buffer.hpp"
#include "frame.hpp"
        
Mjpeg::Frame::Frame(Buffer* buffer) : buffer_(buffer) {
	if (buffer_) {
		buffer_->retain();
	}
}

Mjpeg::Frame::~Frame() {
	if (buffer_) { 
		buffer_->release();
	}
}

Mjpeg::Frame::Frame(Mjpeg::Frame&& other) noexcept : buffer_(other.buffer_)  {
	other.buffer_ = nullptr;
}

Mjpeg::Frame& Mjpeg::Frame::operator=(Frame&& other) noexcept {
	if (this != &other) {
		if (buffer_) { 
			buffer_->release();
		}
		buffer_ = other.buffer_;
		other.buffer_ = nullptr;
	}
	return *this;
}

Mjpeg::Frame::Frame(const Mjpeg::Frame& other) : buffer_(other.buffer_) {
	if (buffer_) {
		buffer_->retain();
	}
}

Mjpeg::Frame& Mjpeg::Frame::operator=(const Frame& other) {
	if (this != &other) {
		if (buffer_) { 
			buffer_->release();
		}
		buffer_ = other.buffer_;
		if (buffer_) { 
			buffer_->retain();
		}
	}
	return *this;
}

Mjpeg::Buffer* Mjpeg::Frame::getBuffer() const { 
	return buffer_; 
}

Mjpeg::Buffer* Mjpeg::Frame::operator->() const { 
	return buffer_; 
}

Mjpeg::Frame::operator bool() const { 
	return buffer_ != nullptr; 
}