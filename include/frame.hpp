#pragma once

#include <atomic>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>
#include "buffer.hpp"

namespace Mjpeg {
    class Frame {
    public:
        Frame() = default;
        
        explicit Frame(Buffer* buffer);
        
        ~Frame();

        Frame(Frame&& other) noexcept;
        
        Frame& operator=(Frame&& other) noexcept;

        Frame(const Frame& other);
        
        Frame& operator=(const Frame& other);

        Buffer* getBuffer() const;

        Buffer* operator->() const;

        explicit operator bool() const;

    private:
        Buffer* buffer_{nullptr};
    };
}