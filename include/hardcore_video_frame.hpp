#pragma once

#include <vector>
#include <cstdint>
#include <cstring>
#include <span>
#include <algorithm>

struct alignas(64) HardcoreVideoFrame {
    // 128 bytes reserved up front for uWebSockets HTTP framing headers
    static constexpr size_t PADDING_SIZE = 128;

    std::vector<uint8_t> storage;
    size_t active_size{0}; // Track bytes written strictly *after* the padding

    // Initialize layout completely up front
    void pre_allocate(size_t frame_reserve_capacity) {
        storage.resize(PADDING_SIZE + frame_reserve_capacity);
        active_size = 0;
    }

    // Fast clear resets the active pointer back to the padding boundary
    void clear() noexcept {
        active_size = 0;
    }

    // Append raw protocol packets straight past the 128-byte margin
    void append_payload(std::span<const uint8_t> src) noexcept {
        size_t write_offset = PADDING_SIZE + active_size;
        if (write_offset + src.size() <= storage.size()) {
            std::memcpy(storage.data() + write_offset, src.data(), src.size());
            active_size += src.size();
        }
    }

    // In-place trimming without modifying underlying allocation size
    void trim(size_t startOffset, size_t endOffset) noexcept {
        if (startOffset < endOffset && endOffset <= active_size) {
            size_t new_length = endOffset - startOffset;
            
            // memmove the valid JPEG bytes down to sit cleanly against the padding edge
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

    // --- Accessor Slices ---

    // Raw payload data (just the pure JPEG bytes)
    [[nodiscard]] std::span<const uint8_t> getContentSlice() const noexcept {
        return { storage.data() + PADDING_SIZE, active_size };
    }

    // The full contiguous block including the 128-byte header zone
    [[nodiscard]] std::span<const uint8_t> getAllocatedSlice() const noexcept {
        return { storage.data(), PADDING_SIZE + active_size };
    }
};
