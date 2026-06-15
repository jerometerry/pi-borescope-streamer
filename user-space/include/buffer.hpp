#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "buffer_recycler.hpp"

/**
 * @brief Pre-allocated contiguous block of memory, used to prevent memory allocations on the hot
 * paths.
 *
 * @details A small block of memory is reserved at the beginning of the contiguous memory block for
 * use by HTTP web servers sending payloads to clients. The web server can use this reserved block
 * of memory to construct the necessary HTTP headers, so that only one send is necessary, allowing
 * the web servers zero-byte allocation routines to be leveraged when possible.
 */
class Buffer {
   public:
    /**
     * @brief Constructs a standalone Buffer without a pool recycler.
     * @details When this buffer's reference count hits zero, it will be destroyed
     * via standard heap deletion.
     * @param paddingSize The number of bytes to permanently reserve at the front of the buffer.
     */
    explicit Buffer(const size_t paddingSize);

    /**
     * @brief Constructs a pooled Buffer linked to a specific recycler.
     * @details When this buffer's reference count hits zero, it will be handed back
     * to the provided recycler (e.g., the BufferPool) instead of being deleted.
     * @param paddingSize The number of bytes to permanently reserve at the front of the buffer.
     * @param recycler Pointer to the interface responsible for reclaiming this buffer.
     */
    explicit Buffer(const size_t paddingSize, BufferRecycler* recycler);

    /**
     * @brief Atomically increments the internal reference count.
     * @details Automatically called by IntrusivePtr copy constructors and assignments.
     */
    void retain();

    /**
     * @brief Atomically decrements the internal reference count.
     * @details Automatically called by IntrusivePtr destructors.
     * @return true if the reference count reached zero and the buffer should be recycled/destroyed.
     */
    bool release();

    /**
     * @brief Resets the buffer's content for reuse.
     * @details Clears out the image payload but safely preserves the reserved HTTP padding
     * so the buffer is immediately ready for the next frame.
     */
    void clear();

    /**
     * @brief Checks if the buffer contains any actual video payload.
     * @return true if there is no image data (ignoring the reserved padding).
     */
    bool empty() const;

    /**
     * @brief Expands the underlying memory capacity to prevent mid-stream reallocations.
     * @param size The expected payload size. (The padding size will be automatically added to
     * this).
     */
    void reserve(size_t size);

    /**
     * @brief Retrieves the first byte of the actual video payload.
     * @return The first byte of the content (skipping the HTTP padding).
     */
    uint8_t front() const;

    /**
     * @brief Retrieves the size of the current video payload.
     * @return The number of bytes currently stored in the content section.
     */
    size_t contentSize() const;

    /**
     * @brief Retrieves the size of the reserved prefix block.
     * @return The fixed padding size (e.g., 128 bytes).
     */
    size_t paddingSize() const;

    /**
     * @brief Retrieves the total active footprint of the buffer.
     * @return The combined size of the HTTP padding and the current video payload.
     */
    size_t totalSize() const;

    /**
     * @brief Retrieves the absolute maximum size the buffer can hold without allocating memory.
     * @return The total capacity of the underlying std::vector.
     */
    size_t totalCapacity() const;

    /**
     * @brief Retrieves the actual video payload data.
     * @return A read-only span of the populated image data, ignoring the HTTP padding.
     */
    std::span<const uint8_t> getContentSlice() const;

    /**
     * @brief Retrieves the reserved prefix memory space.
     * @return A writable span pointing to the 128 bytes immediately preceding the video data,
     * intended for injecting zero-copy network headers.
     */
    std::span<uint8_t> getMutablePaddingSlice();

    /**
     * @brief Appends raw bytes from the USB hardware into the content section.
     * @param content The raw data chunk to append.
     */
    void insertContent(std::span<const uint8_t> content);

    /**
     * @brief Snips the front and back of the content slice to perfectly bound a JPEG image.
     * @param startOffset The index of the Start of Image (FF D8) marker.
     * @param endOffset The index of the End of Image (FF D9) marker.
     */
    void trim(size_t startOffset, size_t endOffset);

    /**
     * @brief Retrieves a read-only view of the reserved prefix memory space.
     * @return A read-only span of the padding section.
     */
    std::span<const uint8_t> getPaddingSlice() const;

    /**
     * @brief Retrieves a continuous read-only view of both the padding and the payload.
     * @details This is the method used by the web server to broadcast the perfectly fused
     * HTTP headers and JPEG data in a single socket system call.
     * @return A read-only span encompassing the entire active buffer.
     */
    std::span<const uint8_t> all() const;

    /**
     * @brief Retrieves a writable view of the actual video payload data.
     * @return A writable span of the content section.
     */
    std::span<uint8_t> getMutableContentSlice();

   private:
    friend void intrusive_ptr_release(Buffer* b);

    void ensurePaddingReserved();

    std::vector<uint8_t>& data();

    std::atomic<int> refCount_{0};
    std::vector<uint8_t> data_;
    const size_t paddingSize_;

    BufferRecycler* recycler_;
};

/**
 * @brief Increments the intrusive reference count for a Buffer object.
 * @details This is a free function designed to be resolved via Argument-Dependent Lookup (ADL)
 * by the IntrusivePtr template. It acts as the bridge between the smart pointer and the
 * Buffer's internal atomic counter.
 * @param b The raw pointer to the Buffer being retained.
 */
inline void intrusive_ptr_add_ref(Buffer* b) {
    b->retain();
}

/**
 * @brief Decrements the intrusive reference count and triggers lifecycle management.
 * @details This free function is resolved via Argument-Dependent Lookup (ADL). It handles
 * the critical memory lifecycle branching logic. When a Buffer's reference count drops to zero,
 * this function checks for the existence of a BufferRecycler.
 * * If a recycler is present, the Buffer is safely handed back to the BufferPool for the next
 * USB interrupt. If no recycler is present (a standalone Buffer), it is permanently destroyed
 * and its memory is freed.
 * @param b The raw pointer to the Buffer being released.
 */
inline void intrusive_ptr_release(Buffer* b) {
    if (b->release()) {
        if (b->recycler_) {
            b->recycler_->recycle(b);
        } else {
            std::default_delete<Buffer>{}(b);
        }
    }
}
