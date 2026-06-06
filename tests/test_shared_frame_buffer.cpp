#include <gtest/gtest.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <thread>
#include <vector>
#include <memory>
#include "buffer_pool.hpp"
#include "constants.hpp"
#include "data_structures.hpp"
#include "shared_frame_buffer.hpp"

static void pushFrame (SharedFrameBuffer& sfb, const std::shared_ptr<BufferPool>& bp, std::vector<uint8_t>& data) {
    USB::FramePtr frame = bp->acquire();
    frame->insert(data);
    sfb.push(frame);
};

TEST(SharedFrameBufferTest, InitializesWithCorrectBufferState) {
    auto bufferPool = BufferPool::create();
    SharedFrameBuffer frameBuffer(bufferPool);

    uint32_t frameId = 99;

    auto activeFrame = frameBuffer.getLatestFrame(frameId);
    
    EXPECT_EQ(activeFrame.get(), nullptr);
    EXPECT_EQ(frameId, 0) << "Frame ID should start strictly at 0";
}

TEST(SharedFrameBufferTest, SafelyHandlesImmediatePollingWithoutSegfault) {
    auto bufferPool = BufferPool::create();
    SharedFrameBuffer frameBuffer(bufferPool);

    uint32_t frameId = 0;

    auto activeFrame = frameBuffer.getLatestFrame(frameId);

    ASSERT_FALSE(activeFrame) << "FramePtr should safely evaluate to false when empty.";

    if (activeFrame) {
        FAIL() << "Execution should not reach this block. Guard check failed.";
    }
}

TEST(SharedFrameBufferTest, DereferencingEmptyFramePtrCausesDeath) {
    auto bufferPool = BufferPool::create();
    SharedFrameBuffer frameBuffer(bufferPool);

    uint32_t frameId = 0;
    auto activeFrame = frameBuffer.getLatestFrame(frameId);

    EXPECT_DEATH({
        activeFrame->size(); 
    }, "");
}

TEST(SharedFrameBufferTest, frameBuffer) {
    auto bufferPool = BufferPool::create();
    SharedFrameBuffer frameBuffer(bufferPool);

    std::vector<uint8_t> frame = { 0xFF, 0xD8, 0xAA, 0xBB, 0xFF, 0xD9 };
    pushFrame(frameBuffer, bufferPool, frame);

    uint32_t currentId = 0;
    auto currentFrame = frameBuffer.getLatestFrame(currentId);

    EXPECT_EQ(currentId, 1);
    ASSERT_NE(currentFrame->data().data(), nullptr);
    EXPECT_EQ(currentFrame->size(), frame.size());
    EXPECT_EQ(currentFrame->data(), frame);
}

TEST(SharedFrameBufferTest, SafelyRejectsEmptyFrames) {
    auto bufferPool = BufferPool::create();
    SharedFrameBuffer frameBuffer(bufferPool);

    std::vector<uint8_t> frameData = { 0x01, 0x02, 0x03 };
    pushFrame(frameBuffer, bufferPool, frameData);
    
    uint32_t initialId = 0;
    auto initialFrame = frameBuffer.getLatestFrame(initialId);
    EXPECT_EQ(initialId, 1);

    std::vector<uint8_t> emptyFrame = {};
    pushFrame(frameBuffer, bufferPool, emptyFrame);    
    
    uint32_t nextId = 0;
    auto nextFrame = frameBuffer.getLatestFrame(nextId);
    
    EXPECT_EQ(initialId, nextId) << "Frame ID should not increment for empty frames.";
    EXPECT_EQ(
        initialFrame.get()->data(), 
        nextFrame.get()->data()
    ) << "Active frame pointer should remain unchanged.";
}

TEST(SharedFrameBufferTest, ConcurrentProducersAndConsumers) {
    auto bufferPool = BufferPool::create();
    SharedFrameBuffer frameBuffer(bufferPool);

    std::atomic<bool> producerDone{false};
    std::atomic<int> framesProduced{0};
    std::atomic<int> totalFramesConsumed{0};
    constexpr int TARGET_FRAMES = 5000;

    std::thread producer([&]() {
        std::vector<uint8_t> frame = { 0xAA, 0xBB, 0xCC };
        
        while (framesProduced.load(std::memory_order_relaxed) < TARGET_FRAMES) {
            pushFrame(frameBuffer, bufferPool, frame);
            framesProduced.fetch_add(1, std::memory_order_relaxed);

            std::this_thread::yield(); 
        }
        producerDone.store(true, std::memory_order_release);
    });

    auto consumerFunc = [&]() {
        uint32_t lastSeenId = 0;
        int localConsumed = 0;

        while (!producerDone.load(std::memory_order_acquire) || lastSeenId < TARGET_FRAMES) {
            
            uint32_t currentId = 0;
            auto frame = frameBuffer.getLatestFrame(currentId);
            
            if (frame && currentId > lastSeenId) {
                localConsumed++;
                lastSeenId = currentId; // Update our bookmark
            }

            if (producerDone.load(std::memory_order_acquire) && lastSeenId >= TARGET_FRAMES) {
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

TEST(SharedFrameBufferTest, BoundedPoolGrowth) {
    auto bufferPool = BufferPool::create();
    SharedFrameBuffer frameBuffer(bufferPool);

    std::vector<USB::FramePtr> slowConsumers;
    std::vector<uint8_t> dummyFrame = { 0xDE, 0xAD, 0xBE, 0xEF };

    constexpr int SPIKE_SIZE = 10;

    for (int i = 0; i < SPIKE_SIZE; ++i) {
        pushFrame(frameBuffer, bufferPool, dummyFrame);
        uint32_t id = 0;
        slowConsumers.push_back(frameBuffer.getLatestFrame(id));
    }

    slowConsumers.clear();

    size_t currentPoolSize = bufferPool->getFreeBuffers();
    
    EXPECT_EQ(currentPoolSize, SharedFrameBufferConfig::MAX_SHARED_FRAME_POOL_SIZE);
}