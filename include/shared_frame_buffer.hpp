#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include "mjpeg_data_structures.hpp"
class BufferPool;

namespace Mjpeg {
    class Frame;
}

class SharedFrameBuffer : public std::enable_shared_from_this<SharedFrameBuffer> {
public:
    SharedFrameBuffer() = default;
    void push(Mjpeg::Frame frame);
    Mjpeg::Frame getLatestFrame(uint32_t& outFrameId) const;

private:
    mutable std::mutex activeMutex_;
    Mjpeg::Frame frame_;

    uint32_t frameId_{0};    
};