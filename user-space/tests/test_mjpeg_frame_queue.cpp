#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "buffer.hpp"
#include "buffer_pool.hpp"
#include "buffer_ptr.hpp"
#include "intrusive_ptr.hpp"
#include "mjpeg_frame_queue.hpp"

static void queueFrame(MjpegFrameQueue& queue, const std::shared_ptr<BufferPool>& pool,
                       std::vector<uint8_t>& frame) {
    BufferPtr buffer = pool->borrow();
    buffer->insertContent(frame);
    queue.push(buffer);
};

TEST(MjpegFrameQueueTest, InitializesWithCorrectBufferState) {
    MjpegFrameQueue frameQueue;

    uint32_t frameId = 99;

    auto activeFrame = frameQueue.pop(frameId);

    EXPECT_EQ(activeFrame.get(), nullptr);
    EXPECT_EQ(frameId, 0) << "BufferPtr ID should start strictly at 0";
}

TEST(MjpegFrameQueueTest, SafelyHandlesImmediatePollingWithoutSegfault) {
    MjpegFrameQueue frameQueue;

    uint32_t frameId = 0;

    auto activeFrame = frameQueue.pop(frameId);

    ASSERT_FALSE(activeFrame) << "BufferPtr should safely evaluate to false when empty.";

    if (activeFrame) {
        FAIL() << "Execution should not reach this block. Guard check failed.";
    }
}

TEST(MjpegFrameQueueTest, PopOnEmptyQueueReturnsNull) {
    MjpegFrameQueue frameQueue;

    uint32_t frameId = 0;
    auto activeFrame = frameQueue.pop(frameId);

    EXPECT_FALSE(activeFrame);
    EXPECT_EQ(activeFrame.get(), nullptr);
}

TEST(MjpegFrameQueueTest, PopRetrievesPushedFrameIntact) {
    auto bufferPool = BufferPool::create();
    MjpegFrameQueue frameQueue;

    std::vector<uint8_t> frame = {0xFF, 0xD8, 0xAA, 0xBB, 0xFF, 0xD9};
    queueFrame(frameQueue, bufferPool, frame);

    uint32_t currentId = 0;
    auto currentFrame = frameQueue.pop(currentId);

    EXPECT_EQ(currentId, 1);
    ASSERT_NE(currentFrame->getContentSlice().data(), nullptr);

    EXPECT_EQ(currentFrame->contentSize(), frame.size());

    EXPECT_THAT(currentFrame->getContentSlice(), ::testing::ElementsAreArray(frame))
        << "The popped payload did not perfectly match the pushed payload.";
}

TEST(MjpegFrameQueueTest, SafelyRejectsEmptyFrames) {
    auto bufferPool = BufferPool::create();
    MjpegFrameQueue frameQueue;

    std::vector<uint8_t> frameData = {0x01, 0x02, 0x03};
    queueFrame(frameQueue, bufferPool, frameData);

    std::vector<uint8_t> emptyFrame = {};
    queueFrame(frameQueue, bufferPool, emptyFrame);

    uint32_t popId = 0;
    auto poppedFrame = frameQueue.pop(popId);

    EXPECT_EQ(popId, 1) << "VideoFrame ID should match the first valid push.";
    ASSERT_TRUE(poppedFrame) << "Popped frame must not be null.";

    std::span<const uint8_t> payload = poppedFrame->getContentSlice();

    EXPECT_THAT(payload, ::testing::ElementsAre(0x01, 0x02, 0x03))
        << "Queue should have retained the valid frame, ignoring the empty push.";

    uint32_t emptyId = 0;
    auto emptyPop = frameQueue.pop(emptyId);
    EXPECT_FALSE(emptyPop) << "Queue should be empty after a destructive pop.";
}

TEST(MjpegFrameQueueTest, ConcurrentProducersAndConsumers) {
    auto bufferPool = BufferPool::create();
    MjpegFrameQueue frameQueue;

    std::atomic<bool> producerDone{false};
    std::atomic<int> framesProduced{0};
    std::atomic<int> totalFramesConsumed{0};
    constexpr int TARGET_FRAMES = 5000;

    std::thread producer([&]() {
        std::vector<uint8_t> frame = {0xAA, 0xBB, 0xCC};

        while (framesProduced.load(std::memory_order_relaxed) < TARGET_FRAMES) {
            queueFrame(frameQueue, bufferPool, frame);
            framesProduced.fetch_add(1, std::memory_order_relaxed);

            std::this_thread::yield();
        }
        producerDone.store(true, std::memory_order_release);
    });

    auto consumerFunc = [&]() {
        uint32_t lastSeenId = 0;
        int localConsumed = 0;

        while (true) {
            uint32_t currentId = 0;
            auto frame = frameQueue.pop(currentId);

            if (frame && currentId > lastSeenId) {
                localConsumed++;
                lastSeenId = currentId;
            }

            if (producerDone.load(std::memory_order_acquire) && currentId >= TARGET_FRAMES) {
                break;
            }

            std::this_thread::yield();
        }

        totalFramesConsumed.fetch_add(localConsumed, std::memory_order_relaxed);
    };

    const int NUM_CONSUMERS = 4;
    std::vector<std::thread> consumers;
    consumers.reserve(NUM_CONSUMERS);
    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        consumers.emplace_back(consumerFunc);
    }

    if (producer.joinable()) {
        producer.join();
    }

    for (auto& consumer : consumers) {
        if (consumer.joinable()) {
            consumer.join();
        }
    }

    EXPECT_GT(totalFramesConsumed.load(std::memory_order_relaxed), 0)
        << "Consumers starved or pipeline failed to serve frames.";
}
