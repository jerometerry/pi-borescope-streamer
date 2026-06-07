#pragma once

#include <atomic>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace Mjpeg {
    struct Buffer;
    using ReturnCallback = void(*)(void*, Buffer*);

    struct Buffer {
    public:
        static constexpr size_t PREFIX_SIZE = 128;

        static size_t prefixSize() {
            return PREFIX_SIZE;
        }
    private:
        std::atomic<int> refCount_{0};
        std::vector<uint8_t> data_;
        ReturnCallback returnCallback_{nullptr};
        void* poolContext_{nullptr};        

        void ensurePrefixReserved() {
            if (data_.size() < prefixSize()) {
                data_.resize(prefixSize());
            }
        }

        std::vector<uint8_t>& data() {
            return data_;
        }        

    public:
        Buffer() {
            ensurePrefixReserved();
        }

        void retain() {
            refCount_.fetch_add(1, std::memory_order_relaxed);
        }
        
        void release() {
            if (refCount_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                if (returnCallback_ && poolContext_) {
                    returnCallback_(poolContext_, this);
                }
            }
        }

        void clear() {
            data_.resize(prefixSize());
        }

        bool empty() const {
            return data_.size() <= prefixSize();
        }

        void reserve(size_t size) {
            data_.reserve(prefixSize() + size);
        }

        void trim(size_t startOffset, size_t endOffset) {
            if (endOffset > contentSize() || startOffset > endOffset) {
                throw std::out_of_range("Invalid trim boundaries");
            }

            size_t internalEnd = prefixSize() + endOffset;
            if (internalEnd < totalSize()) {
                data_.erase(data_.begin() + internalEnd, data_.end());
            }

            if (startOffset > 0) {
                size_t internalStart = prefixSize();
                data_.erase(data_.begin() + internalStart, data_.begin() + internalStart + startOffset);
            }
        }

        void setPoolContext(void* context) {
            poolContext_ = context;
        }

        void setReturnCallback(ReturnCallback callback) {
            returnCallback_ = callback;
        }

        void insertContent(std::span<const uint8_t> newData) {
            ensurePrefixReserved();
            data_.insert(data_.end(), newData.begin(), newData.end());
        }

        uint8_t front() const {
            if (empty()) {
                throw std::out_of_range("Buffer is empty");
            }
            return data_[prefixSize()];
        }

        size_t contentSize() const {
            return data_.size() - prefixSize();
        }

        size_t totalSize() const {
            return data_.size();
        }

        size_t totalCapacity() const {
            return data_.capacity();
        }

        std::span<uint8_t> getMutableContentSlice() {
            return { data_.data() + prefixSize(), contentSize() };
        }

        std::span<const uint8_t> getContentSlice() const {
            return { data_.data() + prefixSize(), contentSize() };
        }

        std::span<const uint8_t> getPrefixSlice() const {
            return { data_.data(), prefixSize() };
        }

        std::span<uint8_t> getMutablePrefixSlice() {
            return { data_.data(), prefixSize() };
        }

        std::span<const uint8_t> all() const {
            return { data_.data(), data_.size() };
        }
    };

    /**
     * @brief
     */
    class Frame {
    public:
        Frame() = default;
        
        explicit Frame(Buffer* buffer) : buffer_(buffer) {
            if (buffer_) {
                buffer_->retain();
            }
        }
        
        ~Frame() {
            if (buffer_) { 
                buffer_->release();
            }
        }

        Frame(Frame&& other) noexcept : buffer_(other.buffer_) {
            other.buffer_ = nullptr;
        }
        
        Frame& operator=(Frame&& other) noexcept {
            if (this != &other) {
                if (buffer_) { 
                    buffer_->release();
                }
                buffer_ = other.buffer_;
                other.buffer_ = nullptr;
            }
            return *this;
        }

        Frame(const Frame& other) : buffer_(other.buffer_) {
            if (buffer_) {
                buffer_->retain();
            }
        }
        
        Frame& operator=(const Frame& other) {
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

        Buffer* getBuffer() const { 
            return buffer_; 
        }

        Buffer* operator->() const { 
            return buffer_; 
        }

        explicit operator bool() const { 
            return buffer_ != nullptr; 
        }

    private:
        Buffer* buffer_{nullptr};
    };
}