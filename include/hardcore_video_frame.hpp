#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

struct alignas(64) HardcoreVideoFrame {
    static constexpr size_t PADDING_SIZE = 128;

    std::vector<uint8_t> storage;

    HardcoreVideoFrame() {
        if (storage.size() < PADDING_SIZE) {
            storage.resize(PADDING_SIZE);
        }
    }

    void ensurePaddingReserved() {
        if (storage.size() < PADDING_SIZE) {
            storage.resize(PADDING_SIZE);
        }
    }

    void pre_allocate(size_t frame_reserve_capacity) {
        storage.resize(PADDING_SIZE + frame_reserve_capacity);
    }

    // void append_payload(std::span<const uint8_t> src) noexcept {
    //     size_t write_offset = PADDING_SIZE + storage;
    //     if (write_offset + src.size() <= storage.size()) {
    //         std::memcpy(storage.data() + write_offset, src.data(), src.size());
    //         active_size += src.size();
    //     }
    // }

    // void trim(size_t startOffset, size_t endOffset) noexcept {
    //     if (startOffset < endOffset && endOffset <= active_size) {
    //         size_t new_length = endOffset - startOffset;

    //         std::memmove(
    //             storage.data() + PADDING_SIZE, 
    //             storage.data() + PADDING_SIZE + startOffset, 
    //             new_length
    //         );
    //         active_size = new_length;
    //     } else {
    //         active_size = 0;
    //     }
    // }

    // [[nodiscard]] std::span<const uint8_t> getContentSlice() const noexcept {
    //     return { storage.data() + PADDING_SIZE, active_size };
    // }

    // [[nodiscard]] std::span<const uint8_t> getAllocatedSlice() const noexcept {
    //     return { storage.data(), PADDING_SIZE + active_size };
    // }

    void clear() {
        storage.resize(PADDING_SIZE);
    }

    bool empty() const {
        return storage.size() <= PADDING_SIZE;
    }

    void reserve(size_t size) {
        storage.reserve(PADDING_SIZE + size);
    }

    void trim(size_t startOffset, size_t endOffset) {
        if (endOffset > contentSize() || startOffset > endOffset) {
            throw std::out_of_range("Invalid trim boundaries");
        }

        size_t internalEnd = PADDING_SIZE + endOffset;
        if (internalEnd < totalSize()) {
            storage.erase(storage.begin() + internalEnd, storage.end());
        }

        if (startOffset > 0) {
            size_t internalStart = PADDING_SIZE;
            storage.erase(storage.begin() + internalStart, storage.begin() + internalStart + startOffset);
        }
    }

    void insertContent(std::span<const uint8_t> content) {
        ensurePaddingReserved();
        storage.insert(storage.end(), content.begin(), content.end());
    }

    uint8_t front() const {
        if (empty()) {
            throw std::out_of_range("Buffer is empty");
        }
        return storage[PADDING_SIZE];
    }

    size_t contentSize() const {
        return storage.size() - PADDING_SIZE;
    }

    size_t paddingSize() const {
        return PADDING_SIZE;
    }

    size_t totalSize() const {
        return storage.size();
    }

    size_t totalCapacity() const {
        return storage.capacity();
    }

    std::span<const uint8_t> getContentSlice() const {
        return { storage.data() + paddingSize(), contentSize() };
    }

    std::span<const uint8_t> getPaddingSlice() const {
        return { storage.data(), paddingSize() };
    }

    std::span<const uint8_t> all() const {
        return { storage.data(), storage.size() };
    }

    std::span<uint8_t> getMutableContentSlice() {
        return { storage.data() + paddingSize(), contentSize() };
    }

    std::span<uint8_t> getMutablePaddingSlice() {
        return { storage.data(), paddingSize() };
    }

    std::vector<uint8_t> data() {
        return storage;
    }
};
