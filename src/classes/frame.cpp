#include "buffer.hpp"
#include "frame.hpp"

Frame::Frame(Buffer* buffer) : buffer_(buffer) {
	if (buffer_) {
		buffer_->retain();
	}
}

Frame::~Frame() {
	if (buffer_) { 
		buffer_->release();
	}
}

Frame::Frame(Frame&& other) noexcept : buffer_(other.buffer_)  {
	other.buffer_ = nullptr;
}

Frame& Frame::operator=(Frame&& other) noexcept {
	if (this != &other) {
		if (buffer_) { 
			buffer_->release();
		}
		buffer_ = other.buffer_;
		other.buffer_ = nullptr;
	}
	return *this;
}

Frame::Frame(const Frame& other) : buffer_(other.buffer_) {
	if (buffer_) {
		buffer_->retain();
	}
}

Frame& Frame::operator=(const Frame& other) {
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

Buffer* Frame::getBuffer() const { 
	return buffer_; 
}

Buffer* Frame::operator->() const { 
	return buffer_; 
}

Frame::operator bool() const { 
	return buffer_ != nullptr; 
}