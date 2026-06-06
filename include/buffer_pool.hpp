#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

class BufferPool : public std::enable_shared_from_this<BufferPool> {
public:
    BufferPool() = default;
    static std::shared_ptr<BufferPool> create();
    
    std::shared_ptr<std::vector<uint8_t>> acquire();
    void release(std::unique_ptr<std::vector<uint8_t>> buffer);
    size_t getFreeBuffers() const;

private:
    void initialize();
    
    mutable std::mutex poolMutex_;
    std::vector<std::unique_ptr<std::vector<uint8_t>>> pool_;
};