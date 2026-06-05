#include <gtest/gtest.h>
#include <atomic>
#include <cstdint>
#include <span>
#include <string>
#include <thread>
#include <vector>
#include <memory>
#include "frame_exchange.hpp"

TEST(FrameExchangeTest, InitializesWithCorrectBufferState) {
    FrameExchange exchange;
    uint32_t frameId = 99; // Set to a garbage value to ensure it gets overwritten

    auto activeFrame = exchange.getLatestFrame(frameId);
    
    ASSERT_NE(activeFrame, nullptr);
    EXPECT_EQ(frameId, 0) << "Frame ID should start strictly at 0";
    EXPECT_TRUE(activeFrame->empty()) << "Initial frame should be an empty canvas";
}

TEST(FrameExchangeTest, PublishFrameUpdatesActiveFrame) {
    FrameExchange exchange;
    
    // 1. Create a fake JPEG payload
    std::vector<uint8_t> fakeCameraData = { 0xFF, 0xD8, 0xAA, 0xBB, 0xFF, 0xD9 };

    // 2. Publish it directly (FrameExchange handles the buffer pool internally)
    exchange.publishFrame(fakeCameraData);

    // 3. Verify the consumer gets the new data and an incremented ID
    uint32_t currentId = 0;
    auto currentFrame = exchange.getLatestFrame(currentId);

    EXPECT_EQ(currentId, 1);
    ASSERT_NE(currentFrame, nullptr);
    EXPECT_EQ(currentFrame->size(), fakeCameraData.size());
    EXPECT_EQ(*currentFrame, fakeCameraData);
}

TEST(FrameExchangeTest, SafelyRejectsEmptyFrames) {
    FrameExchange exchange;
    
    // Setup a valid frame first
    std::vector<uint8_t> validData = { 0x01, 0x02, 0x03 };
    exchange.publishFrame(validData);
    
    uint32_t initialId = 0;
    auto initialFrame = exchange.getLatestFrame(initialId);
    EXPECT_EQ(initialId, 1);

    // Attempt to publish an empty payload
    std::vector<uint8_t> emptyData = {};
    exchange.publishFrame(emptyData);
    
    // Verify the active frame was NOT overwritten and the ID didn't increment
    uint32_t nextId = 0;
    auto nextFrame = exchange.getLatestFrame(nextId);
    
    EXPECT_EQ(initialId, nextId) << "Frame ID should not increment for empty frames.";
    EXPECT_EQ(initialFrame.get(), nextFrame.get()) << "Active frame pointer should remain unchanged.";
}

TEST(FrameExchangeTest, ConcurrentProducersAndConsumers) {
    FrameExchange exchange;
    std::atomic<bool> producerDone{false};
    std::atomic<int> framesProduced{0};
    std::atomic<int> totalFramesConsumed{0};
    constexpr int TARGET_FRAMES = 5000;

    // THE PRODUCER: Just blindly publishes payloads as fast as possible
    std::thread producer([&]() {
        std::vector<uint8_t> dummyFrame = { 0xAA, 0xBB, 0xCC };
        
        while (framesProduced.load(std::memory_order_relaxed) < TARGET_FRAMES) {
            exchange.publishFrame(dummyFrame);
            framesProduced.fetch_add(1, std::memory_order_relaxed);
            
            // Tiny yield to mimic camera hardware delay and let consumers read
            std::this_thread::yield(); 
        }
        producerDone.store(true, std::memory_order_release);
    });

    // THE CONSUMERS: Constantly poll for the newest frame
    auto consumerFunc = [&]() {
        uint32_t lastSeenId = 0;
        int localConsumed = 0;

        while (!producerDone.load(std::memory_order_acquire) || lastSeenId < TARGET_FRAMES) {
            
            uint32_t currentId = 0;
            auto frame = exchange.getLatestFrame(currentId);
            
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

    // Spin up 4 concurrent consumers reading the exact same data
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

    // If the system works, the consumers read frames without deadlocking or segfaulting
    EXPECT_GT(totalFramesConsumed.load(std::memory_order_relaxed), 0) 
        << "Consumers starved or pipeline failed to serve frames.";
}