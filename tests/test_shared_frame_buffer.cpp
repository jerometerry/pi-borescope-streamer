#include <gtest/gtest.h>
#include <atomic>
#include <cstdint>
#include <span>
#include <string>
#include <thread>
#include <vector>
#include <memory>
#include "shared_frame_buffer.hpp"

TEST(FrameExchangeTest, InitializesWithCorrectBufferState) {
    SharedFrameBuffer frameBuffer;
    uint32_t frameId = 99;

    auto activeFrame = frameBuffer.getLatestFrame(frameId);
    
    ASSERT_NE(activeFrame, nullptr);
    EXPECT_EQ(frameId, 0) << "Frame ID should start strictly at 0";
    EXPECT_TRUE(activeFrame->empty()) << "Initial frame should be an empty canvas";
}

TEST(FrameExchangeTest, frameBuffer) {
    SharedFrameBuffer frameBuffer;

    std::vector<uint8_t> frame = { 0xFF, 0xD8, 0xAA, 0xBB, 0xFF, 0xD9 };

    frameBuffer.push(frame);

    uint32_t currentId = 0;
    auto currentFrame = frameBuffer.getLatestFrame(currentId);

    EXPECT_EQ(currentId, 1);
    ASSERT_NE(currentFrame, nullptr);
    EXPECT_EQ(currentFrame->size(), frame.size());
    EXPECT_EQ(*currentFrame, frame);
}

TEST(FrameExchangeTest, SafelyRejectsEmptyFrames) {
    SharedFrameBuffer frameBuffer;

    std::vector<uint8_t> frame = { 0x01, 0x02, 0x03 };
    frameBuffer.push(frame);
    
    uint32_t initialId = 0;
    auto initialFrame = frameBuffer.getLatestFrame(initialId);
    EXPECT_EQ(initialId, 1);

    std::vector<uint8_t> emptyFrame = {};
    frameBuffer.push(emptyFrame);
    
    uint32_t nextId = 0;
    auto nextFrame = frameBuffer.getLatestFrame(nextId);
    
    EXPECT_EQ(initialId, nextId) << "Frame ID should not increment for empty frames.";
    EXPECT_EQ(initialFrame.get(), nextFrame.get()) << "Active frame pointer should remain unchanged.";
}

TEST(FrameExchangeTest, ConcurrentProducersAndConsumers) {
    SharedFrameBuffer frameBuffer;
    std::atomic<bool> producerDone{false};
    std::atomic<int> framesProduced{0};
    std::atomic<int> totalFramesConsumed{0};
    constexpr int TARGET_FRAMES = 5000;

    std::thread producer([&]() {
        std::vector<uint8_t> frame = { 0xAA, 0xBB, 0xCC };
        
        while (framesProduced.load(std::memory_order_relaxed) < TARGET_FRAMES) {
            frameBuffer.push(frame);
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