#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>
#include "buffer.hpp"
#include "constants.hpp"

Buffer::Buffer(const size_t paddingSize) : paddingSize_(paddingSize) {
	if (data_.size() < paddingSize) {
		data_.resize(paddingSize);
	}
}

void Buffer::retain() {
	refCount_.fetch_add(1, std::memory_order_relaxed);
}

bool Buffer::release() {
	return refCount_.fetch_sub(1, std::memory_order_acq_rel) == 1;
}

void Buffer::clear() {
	data_.resize(paddingSize());
}

bool Buffer::empty() const {
	return data_.size() <= paddingSize();
}

void Buffer::reserve(size_t size) {
	data_.reserve(paddingSize() + size);
}

void Buffer::trim(size_t startOffset, size_t endOffset) {
	if (endOffset > contentSize() || startOffset > endOffset) {
		throw std::out_of_range("Invalid trim boundaries");
	}

	size_t internalEnd = paddingSize() + endOffset;
	if (internalEnd < totalSize()) {
		data_.erase(data_.begin() + internalEnd, data_.end());
	}

	if (startOffset > 0) {
		size_t internalStart = paddingSize();
		data_.erase(data_.begin() + internalStart, data_.begin() + internalStart + startOffset);
	}
}

void Buffer::setPoolContext(void* context) {
	poolContext_ = context;
}

void Buffer::setReturnCallback(ReturnCallback callback) {
	returnCallback_ = callback;
}

void Buffer::insertContent(std::span<const uint8_t> content) {
	ensurePaddingReserved();
	data_.insert(data_.end(), content.begin(), content.end());
}

uint8_t Buffer::front() const {
	if (empty()) {
		throw std::out_of_range("Buffer is empty");
	}
	return data_[paddingSize()];
}

size_t Buffer::contentSize() const {
	return data_.size() - paddingSize();
}

size_t Buffer::paddingSize() const {
	return paddingSize_;
}

size_t Buffer::totalSize() const {
	return data_.size();
}

size_t Buffer::totalCapacity() const {
	return data_.capacity();
}

std::span<const uint8_t> Buffer::getContentSlice() const {
	return { data_.data() + paddingSize(), contentSize() };
}

std::span<const uint8_t> Buffer::getPaddingSlice() const {
	return { data_.data(), paddingSize() };
}

std::span<const uint8_t> Buffer::all() const {
	return { data_.data(), data_.size() };
}

std::span<uint8_t> Buffer::getMutableContentSlice() {
	return { data_.data() + paddingSize(), contentSize() };
}

std::span<uint8_t> Buffer::getMutablePaddingSlice() {
	return { data_.data(), paddingSize() };
}

void Buffer::ensurePaddingReserved() {
	if (data_.size() < paddingSize_) {
		data_.resize(paddingSize_);
	}
}

std::vector<uint8_t>& Buffer::data() {
	return data_;
}