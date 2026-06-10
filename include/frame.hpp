#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>
#include <stdexcept>
#include "disruptor.hpp"

struct alignas(disruptor::cache_line_size) Frame {
    static constexpr size_t PADDING_SIZE = 128;

    std::vector<uint8_t> storage;
    size_t active_size{0};

    Frame() {
        storage.resize(paddingSize());
    }

    void preAllocate(size_t frame_reserve_capacity) {
        storage.resize(PADDING_SIZE + frame_reserve_capacity);
        active_size = 0;
    }

    void clear() noexcept {
        active_size = 0;
    }

    bool empty() const noexcept {
        return active_size == 0;
    }

    void insertContent(std::span<const uint8_t> content) {
        size_t write_offset = paddingSize() + active_size;

        if (write_offset + content.size() > storage.size()) {
            storage.resize(write_offset + content.size());
        }

        std::memcpy(storage.data() + write_offset, content.data(), content.size());
        active_size += content.size();
    }

    void trim(size_t startOffset, size_t endOffset) {
        if (endOffset > active_size || startOffset > endOffset) {
            throw std::out_of_range("Invalid trim boundaries");
        }

        size_t new_length = endOffset - startOffset;

        if (startOffset > 0) {
            std::memmove(
                storage.data() + paddingSize(), 
                storage.data() + paddingSize() + startOffset, 
                new_length
            );
        }
        active_size = new_length;
    }

    static size_t paddingSize() noexcept { 
        return PADDING_SIZE; 
    }

    size_t contentSize() const noexcept { 
        return active_size; 
    }

    size_t totalSize() const noexcept { 
        return paddingSize() + active_size; 
}
    
    uint8_t front() const {
        if (empty()) throw std::out_of_range("Buffer is empty");
        return storage[paddingSize()];
    }

    std::span<const uint8_t> getContentSlice() const noexcept {
        return { storage.data() + paddingSize(), active_size };
    }

    std::span<uint8_t> getMutableContentSlice() noexcept {
        return { storage.data() + paddingSize(), active_size };
    }

    std::vector<uint8_t>& data() noexcept {
        return storage;
    }
};
