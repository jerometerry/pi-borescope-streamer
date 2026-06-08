#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

struct Buffer {
public:
    using ReturnCallback = void(*)(void*, Buffer*);

    explicit Buffer(const size_t paddingSize);

    void retain();
    
    bool release();

    void clear();

    bool empty() const;

    void reserve(size_t size);

    void trim(size_t startOffset, size_t endOffset);

    void setPoolContext(void* context);

    void setReturnCallback(ReturnCallback callback);

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
    ReturnCallback returnCallback_{nullptr};
    void* poolContext_{nullptr};
    const size_t paddingSize_;
};

inline void intrusive_ptr_add_ref(Buffer* b) {
    b->retain();
}

inline void intrusive_ptr_release(Buffer* b) {
    if (b->release()) {
        if (b->returnCallback_ && b->poolContext_) {
            b->returnCallback_(b->poolContext_, b);
        } else {
            std::default_delete<Buffer>{}(b);
        }
    }
}