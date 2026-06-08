#include "buffer.hpp"
#include "buffer_ptr.hpp"

BufferPtr::BufferPtr(Buffer* buffer) : buffer_(buffer) {
	if (buffer_) {
		buffer_->retain();
	}
}

BufferPtr::~BufferPtr() {
	if (buffer_) { 
		buffer_->release();
	}
}

BufferPtr::BufferPtr(BufferPtr&& other) noexcept : buffer_(other.buffer_)  {
	other.buffer_ = nullptr;
}

BufferPtr& BufferPtr::operator=(BufferPtr&& other) noexcept {
	if (this != &other) {
		if (buffer_) { 
			buffer_->release();
		}
		buffer_ = other.buffer_;
		other.buffer_ = nullptr;
	}
	return *this;
}

BufferPtr::BufferPtr(const BufferPtr& other) : buffer_(other.buffer_) {
	if (buffer_) {
		buffer_->retain();
	}
}

BufferPtr& BufferPtr::operator=(const BufferPtr& other) {
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

Buffer* BufferPtr::getBuffer() const { 
	return buffer_; 
}

Buffer* BufferPtr::operator->() const { 
	return buffer_; 
}

BufferPtr::operator bool() const { 
	return buffer_ != nullptr; 
}