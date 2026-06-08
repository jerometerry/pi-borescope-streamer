#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <thread>
#include <vector>
#include <memory>
#include "buffer.hpp"
#include "buffer_pool.hpp"
#include "buffer_ptr.hpp"
#include "shared_frame_buffer.hpp"

static void pushFrame (SharedFrameBuffer& sfb, const std::shared_ptr<BufferPool>& bp, std::vector<uint8_t>& data) {
    BufferPtr frame = bp->borrow();
    frame->insertContent(data);
    sfb.push(frame);
};

TEST(SharedFrameBufferTest, InitializesWithCorrectBufferState) {
    SharedFrameBuffer frameBuffer;

    uint32_t frameId = 99;

    auto activeFrame = frameBuffer.getLatestFrame(frameId);
    
    EXPECT_EQ(activeFrame.get(), nullptr);
    EXPECT_EQ(frameId, 0) << "BufferPtr ID should start strictly at 0";
}

TEST(SharedFrameBufferTest, SafelyHandlesImmediatePollingWithoutSegfault) {
    SharedFrameBuffer frameBuffer;

    uint32_t frameId = 0;

    auto activeFrame = frameBuffer.getLatestFrame(frameId);

    ASSERT_FALSE(activeFrame) << "BufferPtr should safely evaluate to false when empty.";

    if (activeFrame) {
        FAIL() << "Execution should not reach this block. Guard check failed.";
    }
}

TEST(SharedFrameBufferTest, DereferencingEmptyFrameCausesDeath) {
    SharedFrameBuffer frameBuffer;

    uint32_t frameId = 0;
    auto activeFrame = frameBuffer.getLatestFrame(frameId);

    EXPECT_DEATH({
        activeFrame->contentSize(); 
    }, "");
}

TEST(SharedFrameBufferTest, frameBuffer) {
    auto bufferPool = BufferPool::create();
    SharedFrameBuffer frameBuffer;

    std::vector<uint8_t> frame = { 0xFF, 0xD8, 0xAA, 0xBB, 0xFF, 0xD9 };
    pushFrame(frameBuffer, bufferPool, frame);

    uint32_t currentId = 0;
    auto currentFrame = frameBuffer.getLatestFrame(currentId);

    EXPECT_EQ(currentId, 1);
    ASSERT_NE(currentFrame->getContentSlice().data(), nullptr);

    EXPECT_EQ(currentFrame->contentSize(), frame.size());

    std::span<const uint8_t> actualPayload = currentFrame->getContentSlice();
    std::span<const uint8_t> expectedPayload(frame.data(), frame.size());
    bool areEqual = std::equal(
        actualPayload.begin(), actualPayload.end(), 
        expectedPayload.begin(), expectedPayload.end()
    );
    EXPECT_TRUE(areEqual);
}

TEST(SharedFrameBufferTest, SafelyRejectsEmptyFrames) {
    auto bufferPool = BufferPool::create();
    SharedFrameBuffer frameBuffer;

    std::vector<uint8_t> frameData = { 0x01, 0x02, 0x03 };
    pushFrame(frameBuffer, bufferPool, frameData);
    
    uint32_t initialId = 0;
    auto initialFrame = frameBuffer.getLatestFrame(initialId);
    EXPECT_EQ(initialId, 1);

    std::vector<uint8_t> emptyFrame = {};
    pushFrame(frameBuffer, bufferPool, emptyFrame);    
    
    uint32_t nextId = 0;
    auto nextFrame = frameBuffer.getLatestFrame(nextId);
    
    EXPECT_EQ(initialId, nextId) << "BufferPtr ID should not increment for empty frames.";

    std::span<const uint8_t> initialPayload = initialFrame->getContentSlice();
    std::span<const uint8_t> nextPayload = nextFrame->getContentSlice();

    EXPECT_THAT(
        initialPayload, 
        ::testing::ElementsAreArray(nextPayload.begin(), nextPayload.end())
    ) << "Active frame pointer should remain unchanged.";
}

TEST(SharedFrameBufferTest, ConcurrentProducersAndConsumers) {
    auto bufferPool = BufferPool::create();
    SharedFrameBuffer frameBuffer;

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
