#pragma once
#include <cstdint>
#include <memory>
#include "buffer_ptr.hpp"
#include "thread_safety.hpp"
#include "thread_safety_mutex.hpp"

class BufferPool;

/**
 * @brief MjpegFrameQueue is a thread safe data structure that stores the latest MJPEG frame from the USB camera feed,
 * overwriting the previous frame.
 *
 * @details MjpegStream fires new frames in the onFrameReady callback, held in a BufferPtr (aka IntrusivePtr<Buffer>).
 * When running the mjpeg_server application, onFrameReady is wired up to send the latest frame into MjpegFrameQueue 
 * via push(). MjpegServer is wired up with FrameSource that reads the latest frame from MjpegFrameQueue via pop().
 *
 * <h3>Example:</h3>
 * <pre><code> 
 * int main() {
 *     auto pool = BufferPool::create();
 *     MjpegFrameQueue queue;
 *     MjpegStream stream(pool, [&queue](const BufferPtr& frame) {
 *         queue.push(frame);
 *     });
 *
 *     auto transfer = [&stream](UsbTransferStatus status, std::span<const uint8_t> payload) -> bool {
 *         if (status == UsbTransferStatus::Completed) {
 *             if (!payload.empty()) {
 *                 stream.send(payload);
 *             }
 *             return true;
 *         }
 *         return status != UsbTransferStatus::Disconnected; 
 *     };
 *     UsbDriver driver(transfer, &running);
 *
 *     auto source = [&queue](uint32_t& id) {
 *         return queue.pop(id);
 *     };
 *     MjpegServer server(port, running, source);
 * 
 *     driver.start(camera);
 *     server.start();
 * }
 * </code></pre>
 * 
 */
class MjpegFrameQueue : public std::enable_shared_from_this<MjpegFrameQueue> {
public:
    MjpegFrameQueue() = default;

    /**
     * @brief Pushes a new frame into the queue, aggressively overwriting the previous frame.
     * @details Safely rejects empty frames. Destroys the overwritten frame outside the internal 
     * mutex lock to guarantee immunity from lock-inversion deadlocks with the BufferPool.
     * 
     * @param frame The latest MJPEG picture captured from the hardware.
     */
    void push(BufferPtr frame);

    /**
     * @brief Destructively reads the latest frame from the queue.
     * @details This acts as a destructive reader. The moment the frame is popped, ownership is 
     * transferred via std::move, and the queue's internal state becomes empty until the next push().
     * 
     * @param[out] outFrameId Populates with the internal sequential ID of the queue's current state, 
     * even if the queue is currently empty.
     * @return The latest frame, or a null BufferPtr if the queue is empty.
     */
    BufferPtr pop(uint32_t& outFrameId);

private:
    mutable Mutex activeMutex_;

    BufferPtr frame_ GUARDED_BY(activeMutex_);
    uint32_t frameId_ GUARDED_BY(activeMutex_){0};
};
