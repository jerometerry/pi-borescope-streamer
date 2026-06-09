#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
#include "buffer_recycler.hpp"

/**
 * @brief Pre-allocated contiguous block of memory, used to prevent memory allocations on the hot paths. 
 * 
 * @details A small block of memory is reserved at the beginning of the contiguous memory block for use by 
 * HTTP web servers sending payloads to clients. The web server can use this reserved block of memory to construct 
 * the necessary HTTP headers, so that only one send is necessary, allowing the web servers zero-byte allocation 
 * routines to be leveraged when possible.
 */
class Buffer {
public:
    explicit Buffer(const size_t paddingSize);

    explicit Buffer(const size_t paddingSize, BufferRecycler* recycler);

    void retain();
    
    bool release();

    void clear();

    bool empty() const;

    void reserve(size_t size);

    void trim(size_t startOffset, size_t endOffset);

    void insertContent(std::span<const uint8_t> content);

    uint8_t front() const;

    size_t contentSize() const;

    size_t paddingSize() const;

    size_t totalSize() const;

    size_t totalCapacity() const;

    std::span<const uint8_t> getContentSlice() const;

    std::span<const uint8_t> getPaddingSlice() const;

    std::span<const uint8_t> all() const;

    std::span<uint8_t> getMutableContentSlice();

    std::span<uint8_t> getMutablePaddingSlice();

private:
    friend void intrusive_ptr_release(Buffer* b);

    void ensurePaddingReserved();

    std::vector<uint8_t>& data();

    std::atomic<int> refCount_{0};
    std::vector<uint8_t> data_;
    const size_t paddingSize_;

    BufferRecycler* recycler_;
};

inline void intrusive_ptr_add_ref(Buffer* b) {
    b->retain();
}

inline void intrusive_ptr_release(Buffer* b) {
    if (b->release()) {
        if (b->recycler_) {
            b->recycler_->recycle(b);
        } else {
            std::default_delete<Buffer>{}(b);
        }
    }
}
