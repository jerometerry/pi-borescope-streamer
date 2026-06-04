#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>
#include <memory>

#include "shared_frame_pipeline.hpp"
#include "server_constants.hpp"

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

// TEST(SharedFramePipelineTest, RecyclesExactMemoryAddresses) {
//     SharedFramePipeline pipeline;

//     auto originalBuffer = pipeline.checkoutBuffer();
//     ASSERT_NE(originalBuffer, nullptr);
//     auto rawAddress = originalBuffer.get();

//     pipeline.returnBuffer(std::move(originalBuffer));

//     auto recycledBuffer = pipeline.checkoutBuffer();
//     EXPECT_EQ(recycledBuffer.get(), rawAddress) 
//         << "The pipeline dynamically allocated a new vector instead of recycling!";
// }

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
    std::atomic<bool> running{true};
    std::atomic<int> framesProduced{0};
    constexpr int TARGET_FRAMES = 5000;

    std::thread producer([&]() {
        while (framesProduced.load(std::memory_order_relaxed) < TARGET_FRAMES) {
            auto buf = pipeline.checkoutBuffer();
            if (buf) {
                buf->push_back(0xAA); 
                pipeline.updateFrame(std::move(buf));
                framesProduced.fetch_add(1, std::memory_order_release);
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        }
        running.store(false, std::memory_order_release);
    });

    auto consumerFunc = [&]() {
        uint32_t lastSeenId = 0;
        while (running.load(std::memory_order_acquire)) {

            {
                uint32_t currentId = 0;
                auto frame = pipeline.getCurrentFrame(currentId);
                if (frame) {
                    EXPECT_GE(currentId, lastSeenId);
                    lastSeenId = currentId;
                }
            }
            
            std::this_thread::yield(); 
        }
    };

    std::vector<std::thread> consumers;
    const int NUM_CONSUMERS = 4;
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

    EXPECT_EQ(framesProduced.load(std::memory_order_acquire), TARGET_FRAMES);
}