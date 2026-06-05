#include <gtest/gtest.h>
#include <atomic>
#include <cstdint>
#include <span>
#include <string>
#include <thread>
#include <vector>
#include <memory>
#include "server_constants.hpp"
#include "shared_frame_buffer.hpp"

TEST(SharedFrameBufferTest, InitializesWithCorrectBufferState) {
    auto frameBuffer = std::make_shared<SharedFrameBuffer>();
    uint32_t frameId = 99;

    auto activeFrame = frameBuffer->getLatestFrame(frameId);
    
    EXPECT_EQ(activeFrame, nullptr);
    EXPECT_EQ(frameId, 0) << "Frame ID should start strictly at 0";
}

TEST(SharedFrameBufferTest, frameBuffer) {
    auto frameBuffer = std::make_shared<SharedFrameBuffer>();

    std::vector<uint8_t> frame = { 0xFF, 0xD8, 0xAA, 0xBB, 0xFF, 0xD9 };

    frameBuffer->push(frame);

    uint32_t currentId = 0;
    auto currentFrame = frameBuffer->getLatestFrame(currentId);

    EXPECT_EQ(currentId, 1);
    ASSERT_NE(currentFrame, nullptr);
    EXPECT_EQ(currentFrame->size(), frame.size());
    EXPECT_EQ(*currentFrame, frame);
}

TEST(SharedFrameBufferTest, SafelyRejectsEmptyFrames) {
    auto frameBuffer = std::make_shared<SharedFrameBuffer>();

    std::vector<uint8_t> frame = { 0x01, 0x02, 0x03 };
    frameBuffer->push(frame);
    
    uint32_t initialId = 0;
    auto initialFrame = frameBuffer->getLatestFrame(initialId);
    EXPECT_EQ(initialId, 1);

    std::vector<uint8_t> emptyFrame = {};
    frameBuffer->push(emptyFrame);
    
    uint32_t nextId = 0;
    auto nextFrame = frameBuffer->getLatestFrame(nextId);
    
    EXPECT_EQ(initialId, nextId) << "Frame ID should not increment for empty frames.";
    EXPECT_EQ(initialFrame.get(), nextFrame.get()) << "Active frame pointer should remain unchanged.";
}

TEST(SharedFrameBufferTest, ConcurrentProducersAndConsumers) {
    auto frameBuffer = std::make_shared<SharedFrameBuffer>();
    std::atomic<bool> producerDone{false};
    std::atomic<int> framesProduced{0};
    std::atomic<int> totalFramesConsumed{0};
    constexpr int TARGET_FRAMES = 5000;

    std::thread producer([&]() {
        std::vector<uint8_t> frame = { 0xAA, 0xBB, 0xCC };
        
        while (framesProduced.load(std::memory_order_relaxed) < TARGET_FRAMES) {
            frameBuffer->push(frame);
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
            auto frame = frameBuffer->getLatestFrame(currentId);
            
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
    auto frameBuffer = std::make_shared<SharedFrameBuffer>();

    std::vector<std::shared_ptr<const std::vector<uint8_t>>> slowConsumers;
    std::vector<uint8_t> dummyFrame = { 0xDE, 0xAD, 0xBE, 0xEF };

    constexpr int SPIKE_SIZE = 10;

    for (int i = 0; i < SPIKE_SIZE; ++i) {
        frameBuffer->push(dummyFrame);
        uint32_t id = 0;
        slowConsumers.push_back(frameBuffer->getLatestFrame(id));
    }

    slowConsumers.clear();

    size_t currentPoolSize = frameBuffer->getFreePoolSize();
    
    EXPECT_EQ(currentPoolSize, ServerConstants::MAX_SHARED_FRAME_POOL_SIZE);
}