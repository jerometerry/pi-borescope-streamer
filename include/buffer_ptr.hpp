#pragma once

#include <atomic>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>
#include "buffer.hpp"

class BufferPtr {
public:
    BufferPtr() = default;
    
    explicit BufferPtr(Buffer* buffer);
    
    ~BufferPtr();

    BufferPtr(BufferPtr&& other) noexcept;
    
    BufferPtr& operator=(BufferPtr&& other) noexcept;

    BufferPtr(const BufferPtr& other);
    
    BufferPtr& operator=(const BufferPtr& other);

    Buffer* getBuffer() const;

    Buffer* operator->() const;

    explicit operator bool() const;

private:
    Buffer* buffer_{nullptr};
};
