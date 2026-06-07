#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>
#include "buffer.hpp"
#include "constants.hpp"

Mjpeg::Buffer::Buffer(const size_t paddingSize) : paddingSize_(paddingSize) {
	if (data_.size() < paddingSize) {
		data_.resize(paddingSize);
	}
}

std::unique_ptr<Mjpeg::Buffer> Mjpeg::Buffer::unique() {
	return std::make_unique<Mjpeg::Buffer>(BufferPoolConfig::BUFFER_PADDING);
}

std::unique_ptr<Mjpeg::Buffer> Mjpeg::Buffer::unique(size_t padding) {
	return std::make_unique<Mjpeg::Buffer>(padding);
}

void Mjpeg::Buffer::retain() {
	refCount_.fetch_add(1, std::memory_order_relaxed);
}

void Mjpeg::Buffer::release() {
	if (refCount_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
		if (returnCallback_ && poolContext_) {
			returnCallback_(poolContext_, this);
		}
	}
}

void Mjpeg::Buffer::clear() {
	data_.resize(paddingSize());
}

bool Mjpeg::Buffer::empty() const {
	return data_.size() <= paddingSize();
}

void Mjpeg::Buffer::reserve(size_t size) {
	data_.reserve(paddingSize() + size);
}

void Mjpeg::Buffer::trim(size_t startOffset, size_t endOffset) {
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

void Mjpeg::Buffer::setPoolContext(void* context) {
	poolContext_ = context;
}

void Mjpeg::Buffer::setReturnCallback(ReturnCallback callback) {
	returnCallback_ = callback;
}

void Mjpeg::Buffer::insertContent(std::span<const uint8_t> content) {
	ensurePaddingReserved();
	data_.insert(data_.end(), content.begin(), content.end());
}

uint8_t Mjpeg::Buffer::front() const {
	if (empty()) {
		throw std::out_of_range("Buffer is empty");
	}
	return data_[paddingSize()];
}

size_t Mjpeg::Buffer::contentSize() const {
	return data_.size() - paddingSize();
}

size_t Mjpeg::Buffer::paddingSize() const {
	return paddingSize_;
}

size_t Mjpeg::Buffer::totalSize() const {
	return data_.size();
}

size_t Mjpeg::Buffer::totalCapacity() const {
	return data_.capacity();
}

std::span<const uint8_t> Mjpeg::Buffer::getContentSlice() const {
	return { data_.data() + paddingSize(), contentSize() };
}

std::span<const uint8_t> Mjpeg::Buffer::getPaddingSlice() const {
	return { data_.data(), paddingSize() };
}

std::span<const uint8_t> Mjpeg::Buffer::getPrefixSlice() const {
	return getPaddingSlice();
}

std::span<const uint8_t> Mjpeg::Buffer::all() const {
	return { data_.data(), data_.size() };
}

std::span<uint8_t> Mjpeg::Buffer::getMutableContentSlice() {
	return { data_.data() + paddingSize(), contentSize() };
}

std::span<uint8_t> Mjpeg::Buffer::getMutablePaddingSlice() {
	return { data_.data(), paddingSize() };
}

std::span<uint8_t> Mjpeg::Buffer::getMutablePrefixSlice() {
	return getMutablePaddingSlice();
}

void Mjpeg::Buffer::ensurePaddingReserved() {
	if (data_.size() < paddingSize_) {
		data_.resize(paddingSize_);
	}
}

std::vector<uint8_t>& Mjpeg::Buffer::data() {
	return data_;
}