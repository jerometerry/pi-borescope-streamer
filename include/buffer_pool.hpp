#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

class BufferPool : public std::enable_shared_from_this<BufferPool> {
private:
    struct PrivateConstructTag {};

public:
    explicit BufferPool(PrivateConstructTag) {}
    
    static std::shared_ptr<BufferPool> create();
    
    std::shared_ptr<std::vector<uint8_t>> acquire();
    
    size_t getFreeBuffers() const;

private:
    void initialize();
    void release(std::unique_ptr<std::vector<uint8_t>> buffer);
    
    mutable std::mutex poolMutex_;
    std::vector<std::unique_ptr<std::vector<uint8_t>>> pool_;
};