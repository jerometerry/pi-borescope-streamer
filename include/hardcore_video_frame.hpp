#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

struct alignas(64) HardcoreVideoFrame {
    static constexpr size_t PADDING_SIZE = 128;

    std::vector<uint8_t> storage;
    size_t active_size{0};

    void pre_allocate(size_t frame_reserve_capacity) {
        storage.resize(PADDING_SIZE + frame_reserve_capacity);
        active_size = 0;
    }

    void clear() noexcept {
        active_size = 0;
    }

    void append_payload(std::span<const uint8_t> src) noexcept {
        size_t write_offset = PADDING_SIZE + active_size;
        if (write_offset + src.size() <= storage.size()) {
            std::memcpy(storage.data() + write_offset, src.data(), src.size());
            active_size += src.size();
        }
    }

    void trim(size_t startOffset, size_t endOffset) noexcept {
        if (startOffset < endOffset && endOffset <= active_size) {
            size_t new_length = endOffset - startOffset;

            std::memmove(
                storage.data() + PADDING_SIZE, 
                storage.data() + PADDING_SIZE + startOffset, 
                new_length
            );
            active_size = new_length;
        } else {
            active_size = 0;
        }
    }

    [[nodiscard]] std::span<const uint8_t> getContentSlice() const noexcept {
        return { storage.data() + PADDING_SIZE, active_size };
    }

    [[nodiscard]] std::span<const uint8_t> getAllocatedSlice() const noexcept {
        return { storage.data(), PADDING_SIZE + active_size };
    }
};
