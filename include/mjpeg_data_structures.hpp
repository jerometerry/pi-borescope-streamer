#pragma once

#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

namespace Mjpeg {
    struct Buffer {
        std::vector<uint8_t> data_;
        std::atomic<int> refCount_{0};

        void (*returnCallback)(void* context, Buffer* frame){nullptr};
        void* poolContext{nullptr};
        
        void release() {
            if (refCount_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                if (returnCallback && poolContext) {
                    returnCallback(poolContext, this);
                }
            }
        }

        void clear() {
            data_.clear();
        }

        bool empty() const {
            return data_.empty();
        }

        uint8_t front() const {
            return data_.front();
        }

        size_t size() const {
            return data_.size();
        }

        std::vector<uint8_t>& data() {
            return data_;
        }

        void insert(std::span<const uint8_t> newData) {
            data_.insert(data_.end(), newData.begin(), newData.end());
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
                frame_->refCount_.fetch_add(1, std::memory_order_relaxed);
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
                if (frame_) frame_->release();
                frame_ = other.frame_;
                other.frame_ = nullptr;
            }
            return *this;
        }

        Frame(const Frame& other) : frame_(other.frame_) {
            if (frame_) {
                frame_->refCount_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        
        Frame& operator=(const Frame& other) {
            if (this != &other) {
                if (frame_) { 
                    frame_->release();
                }
                frame_ = other.frame_;
                if (frame_) { 
                    frame_->refCount_.fetch_add(1, std::memory_order_relaxed);
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