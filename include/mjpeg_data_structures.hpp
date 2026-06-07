#pragma once

#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

namespace Mjpeg {
    struct Buffer;
    using ReturnCallback = void(*)(void*, Buffer*);

    struct Buffer {
    private:
        static constexpr size_t K_PREFIX_OFFSET = 128;

        std::atomic<int> refCount_{0};
        std::vector<uint8_t> data_;
        ReturnCallback returnCallback_{nullptr};
        void* poolContext_{nullptr};        

        void ensure_prefix() {
            if (data_.size() < K_PREFIX_OFFSET) {
                data_.resize(K_PREFIX_OFFSET);
            }
        }

        std::vector<uint8_t>& data() {
            return data_;
        }        

    public:
        Buffer() {
            ensure_prefix();
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
            data_.resize(K_PREFIX_OFFSET);
        }

        bool empty() const {
            return data_.size() <= K_PREFIX_OFFSET;
        }

        void reserve(size_t size) {
            data_.reserve(K_PREFIX_OFFSET + size);
        }

        void trim(size_t startOffset, size_t endOffset) {
            if (endOffset > size() || startOffset > endOffset) {
                throw std::out_of_range("Invalid trim boundaries");
            }

            size_t internalEnd = K_PREFIX_OFFSET + endOffset;
            if (internalEnd < data_.size()) {
                data_.erase(data_.begin() + internalEnd, data_.end());
            }

            if (startOffset > 0) {
                size_t internalStart = K_PREFIX_OFFSET;
                data_.erase(data_.begin() + internalStart, data_.begin() + internalStart + startOffset);
            }
        }

        void setPoolContext(void* context) {
            poolContext_ = context;
        }

        void setReturnCallback(ReturnCallback callback) {
            returnCallback_ = callback;
        }

        uint8_t front() const {
            if (empty()) {
                throw std::out_of_range("Buffer is empty");
            }
            return data_[K_PREFIX_OFFSET];
        }

        size_t size() const {
            return data_.size() - K_PREFIX_OFFSET;
        }

        std::span<uint8_t> mutable_view() {
            return { data_.data() + K_PREFIX_OFFSET, size() };
        }

        std::span<const uint8_t> view() const {
            return { data_.data() + K_PREFIX_OFFSET, size() };
        }

        void insert(std::span<const uint8_t> newData) {
            ensure_prefix();
            data_.insert(data_.end(), newData.begin(), newData.end());
        }

        std::span<uint8_t> internal_raw_prefix() {
            return { data_.data(), K_PREFIX_OFFSET };
        }
    };

    /**
     * @brief
     */
    class Frame {
    public:
        Frame() = default;
        
        explicit Frame(Buffer* frame) : frame_(frame) {
            if (frame_) {
                frame_->retain();
            }
        }
        
        ~Frame() {
            if (frame_) { 
                frame_->release();
            }
        }

        Frame(Frame&& other) noexcept : frame_(other.frame_) {
            other.frame_ = nullptr;
        }
        
        Frame& operator=(Frame&& other) noexcept {
            if (this != &other) {
                if (frame_) { 
                    frame_->release();
                }
                frame_ = other.frame_;
                other.frame_ = nullptr;
            }
            return *this;
        }

        Frame(const Frame& other) : frame_(other.frame_) {
            if (frame_) {
                frame_->retain();
            }
        }
        
        Frame& operator=(const Frame& other) {
            if (this != &other) {
                if (frame_) { 
                    frame_->release();
                }
                frame_ = other.frame_;
                if (frame_) { 
                    frame_->retain();
                }
            }
            return *this;
        }

        Buffer* get() const { 
            return frame_; 
        }

        Buffer* operator->() const { 
            return frame_; 
        }

        explicit operator bool() const { 
            return frame_ != nullptr; 
        }

    private:
        Buffer* frame_{nullptr};
    };
}