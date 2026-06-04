#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <memory>
#include <unordered_set>
#include "shared_frame_pipeline.hpp"

TEST(SharedFramePipelineTest, InitializesWithCorrectBufferState) {
    SharedFramePipeline pipeline;
    uint32_t frameId = 99; // Set to a garbage value to ensure it gets overwritten

    auto activeFrame = pipeline.getCurrentFrame(frameId);
    ASSERT_NE(activeFrame, nullptr);
    EXPECT_EQ(frameId, 0); // Frame ID should start strictly at 0

    auto buf1 = pipeline.checkoutBuffer();
    auto buf2 = pipeline.checkoutBuffer();
    auto buf3 = pipeline.checkoutBuffer();
    
    EXPECT_NE(buf1, nullptr);
    EXPECT_NE(buf2, nullptr);
    EXPECT_NE(buf3, nullptr);

    auto buf4 = pipeline.checkoutBuffer();
    EXPECT_EQ(buf4, nullptr);
}

TEST(SharedFramePipelineTest, RecyclesExactMemoryAddresses) {
    SharedFramePipeline pipeline;
    std::unordered_set<void*> originalAddresses;
    std::vector<decltype(pipeline.checkoutBuffer())> checkedOutBuffers;

    while (auto buf = pipeline.checkoutBuffer()) {
        originalAddresses.insert(buf.get()); 
        checkedOutBuffers.push_back(std::move(buf));
    }

    ASSERT_FALSE(originalAddresses.empty());

    for (auto& buf : checkedOutBuffers) {
        pipeline.returnBuffer(std::move(buf)); 
    }
    checkedOutBuffers.clear();

    int recycledCount = 0;
    while (auto buf = pipeline.checkoutBuffer()) {
        EXPECT_TRUE(originalAddresses.contains(buf.get())) 
            << "Unrecognized memory address! The pipeline allocated new memory.";
        checkedOutBuffers.push_back(std::move(buf));
        recycledCount++;
    }

    EXPECT_EQ(recycledCount, originalAddresses.size());
}

TEST(SharedFramePipelineTest, UpdateFrameRecyclesOldActiveFrame) {
    SharedFramePipeline pipeline;
    
    uint32_t initialId = 0;
    auto initialFrame = pipeline.getCurrentFrame(initialId);

    auto newBuf = pipeline.checkoutBuffer();
    newBuf->push_back(0xFF); 

    pipeline.updateFrame(newBuf);

    uint32_t newId = 0;
    auto currentFrame = pipeline.getCurrentFrame(newId);
    EXPECT_EQ(newId, 1);
    EXPECT_EQ(currentFrame->size(), 1);
    EXPECT_EQ((*currentFrame)[0], 0xFF);

    auto recycledOldFrame = pipeline.checkoutBuffer();
    EXPECT_EQ(recycledOldFrame.get(), initialFrame.get());
}

TEST(SharedFramePipelineTest, SafelyRejectsNullAndEmptyFrames) {
    SharedFramePipeline pipeline;
    
    uint32_t initialId = 0;
    pipeline.getCurrentFrame(initialId);

    pipeline.updateFrame(nullptr);
    uint32_t nextId = 0;
    pipeline.getCurrentFrame(nextId);
    EXPECT_EQ(initialId, nextId);

    auto emptyBuf = pipeline.checkoutBuffer();
    auto emptyAddress = emptyBuf.get();
    
    pipeline.updateFrame(std::move(emptyBuf));
    
    pipeline.getCurrentFrame(nextId);
    EXPECT_EQ(initialId, nextId);
    
    auto recoveredBuf = pipeline.checkoutBuffer();
    EXPECT_EQ(recoveredBuf.get(), emptyAddress);
}

TEST(SharedFramePipelineTest, ConcurrentProducersAndConsumers) {
    SharedFramePipeline pipeline;
    std::atomic<bool> producerDone{false};
    std::atomic<int> framesProduced{0};
    std::atomic<int> totalFramesConsumed{0};
    constexpr int TARGET_FRAMES = 5000;

    std::thread producer([&]() {
        while (framesProduced.load(std::memory_order_relaxed) < TARGET_FRAMES) {
            auto buf = pipeline.checkoutBuffer();
            if (buf) {
                buf->push_back(0xAA); 
                pipeline.updateFrame(std::move(buf));
                framesProduced.fetch_add(1, std::memory_order_relaxed);
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        }
        producerDone.store(true, std::memory_order_release);
    });

    auto consumerFunc = [&]() {
        uint32_t lastSeenId = 0;
        int localConsumed = 0;

        while (!producerDone.load(std::memory_order_acquire) || lastSeenId < TARGET_FRAMES) {
            
            uint32_t currentId = 0;
            auto frame = pipeline.getCurrentFrame(currentId);
            
            if (frame) {
                EXPECT_GE(currentId, lastSeenId);
                if (currentId > lastSeenId) {
                    localConsumed++;
                    lastSeenId = currentId;
                }
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