#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>
#include <stdexcept>
#include "disruptor.hpp"

struct alignas(disruptor::cache_line_size) Array {
    std::vector<uint8_t> storage_;
    size_t activeSize_{0};


    void preAllocate(size_t frame_reserve_capacity) {
        storage_.resize(frame_reserve_capacity);
        activeSize_ = 0;
    }

    void clear() noexcept {
        activeSize_ = 0;
    }

    bool empty() const noexcept {
        return activeSize_ == 0;
    }

    void insert(std::span<const uint8_t> content) {
        size_t write_offset = activeSize_;

        if (write_offset + content.size() > storage_.size()) {
            storage_.resize(write_offset + content.size());
        }

        std::memcpy(storage_.data() + write_offset, content.data(), content.size());
        activeSize_ += content.size();
    }

    size_t size() const noexcept { 
        return activeSize_; 
    }
    
    uint8_t front() const {
        if (empty()) { 
            throw std::out_of_range("Buffer is empty");
        }
        return storage_.front();
    }

    std::vector<uint8_t>& data() noexcept {
        return storage_;
    }
};
