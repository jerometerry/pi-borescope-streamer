#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <atomic>

extern std::atomic<bool> running; // Global atomic state

class FrameRingBuffer {
private:
    std::vector<std::shared_ptr<Mjpeg::Buffer>> pool;
    size_t head = 0;
    size_t tail = 0;
    size_t capacity;
    std::mutex mtx;
    std::condition_variable cv;

public:
    explicit FrameRingBuffer(size_t size) : capacity(size + 1) {
        pool.resize(capacity);
        // Pre-allocate the user-space buffer pool to minimize heap churn
        for (size_t i = 0; i < capacity; ++i) {
            pool[i] = std::make_shared<Mjpeg::Buffer>();
        }
    }

    // Called by the libusb capture thread
    void push(std::shared_ptr<Mjpeg::Buffer> new_frame) {
        std::lock_guard<std::mutex> lock(mtx);
        
        pool[head] = new_frame;
        head = (head + 1) % capacity;
        
        // If the ring is full, advance the tail to overwrite the oldest frame
        if (head == tail) {
            tail = (tail + 1) % capacity;
        }
        
        cv.notify_one();
    }

    // Called by the uWebSockets or V4L2 network/consumer threads
    std::shared_ptr<Mjpeg::Buffer> pop() {
        std::unique_lock<std::mutex> lock(mtx);
        
        cv.wait(lock, [this]() { 
            return head != tail || !running; 
        });

        if (!running) {
            return nullptr;
        }

        auto frame = pool[tail];
        tail = (tail + 1) % capacity;
        
        return frame;
    }
};
