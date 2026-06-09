#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>
#include "buffer.hpp"
#include "buffer_ptr.hpp"
#include "buffer_pool.hpp"
#include "intrusive_ptr.hpp"
#include "mjpeg_frame_ring_buffer.hpp"

namespace {
    BufferPtr createTestFrame(const std::shared_ptr<BufferPool>& pool, const std::vector<uint8_t>& data) {
        auto frame = pool->borrow();
        if (!data.empty()) {
            frame->insertContent(data);
        }
        return frame;
    }
}

class MjpegFrameRingBufferTest : public ::testing::Test {
private:
    std::shared_ptr<BufferPool> pool_;
    std::atomic<bool> running_{true};

protected:
    void SetUp() override {
        pool_ = BufferPool::create();
        running_.store(true, std::memory_order_release);
    }

    void TearDown() override {
        running_.store(false, std::memory_order_release);
    }

	std::shared_ptr<BufferPool>& getPool() {
		return pool_;
	}

	std::atomic<bool>& getRunning() {
		return running_;
	}

};

TEST_F(MjpegFrameRingBufferTest, PushPop_BasicFifoOrder) {
    MjpegFrameRingBuffer ring(5, getRunning());

    ring.push(createTestFrame(getPool(), {0x01, 0x02}));
    ring.push(createTestFrame(getPool(), {0x03, 0x04}));

    auto frame1 = ring.pop();
    ASSERT_TRUE(frame1);
    EXPECT_THAT(frame1->getContentSlice(), ::testing::ElementsAre(0x01, 0x02));

    auto frame2 = ring.pop();
    ASSERT_TRUE(frame2);
    EXPECT_THAT(frame2->getContentSlice(), ::testing::ElementsAre(0x03, 0x04));
}

TEST_F(MjpegFrameRingBufferTest, Push_SafelyIgnoresEmptyFrames) {
    MjpegFrameRingBuffer ring(3, getRunning());

    BufferPtr nullFrame;
    ring.push(nullFrame);

    ring.push(createTestFrame(getPool(), {})); 

    getRunning().store(false, std::memory_order_release);
    ring.shutdown();

    EXPECT_FALSE(ring.pop());
}

TEST_F(MjpegFrameRingBufferTest, Push_OverflowEnforcesDropOldestPolicy) {
    constexpr size_t CAPACITY = 3;
    MjpegFrameRingBuffer ring(CAPACITY, getRunning());

    for (uint8_t i = 1; i <= 5; ++i) {
        ring.push(createTestFrame(getPool(), {i}));
    }

    for (uint8_t expectedVal = 3; expectedVal <= 5; ++expectedVal) {
        auto frame = ring.pop();
        ASSERT_TRUE(frame) << "Expected frame " << static_cast<int>(expectedVal) << " was missing.";
        EXPECT_THAT(frame->getContentSlice(), ::testing::ElementsAre(expectedVal));
    }

    getRunning().store(false, std::memory_order_release);
    ring.shutdown();
    EXPECT_FALSE(ring.pop());
}

TEST_F(MjpegFrameRingBufferTest, Pop_ShutdownSafelyUnblocksSleepingConsumers) {
    MjpegFrameRingBuffer ring(5, getRunning());
    std::atomic<bool> threadExitedCleanly{false};

    std::thread consumer([&]() {
        auto frame = ring.pop();
        EXPECT_FALSE(frame); 
        threadExitedCleanly.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    getRunning().store(false, std::memory_order_release);
    ring.shutdown();

    consumer.join();

    EXPECT_TRUE(threadExitedCleanly.load(std::memory_order_acquire)) 
        << "Consumer thread failed to wake up and exit after shutdown().";
}

TEST_F(MjpegFrameRingBufferTest, ThreadSafety_ConcurrentProducersAndConsumers) {
    constexpr size_t CAPACITY = 10;
    constexpr int TARGET_FRAMES = 1000;
    
    MjpegFrameRingBuffer ring(CAPACITY, getRunning());
    
    std::atomic<int> pushCount{0};
    std::atomic<int> popCount{0};

    std::thread producer([&]() {
        for (int i = 0; i < TARGET_FRAMES; ++i) {
            ring.push(createTestFrame(getPool(), {0xFF}));
            pushCount.fetch_add(1, std::memory_order_relaxed);
            
            if (i % 10 == 0) {
                // Ensure producers don't starve consumers
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&]() {
        while (getRunning().load(std::memory_order_acquire)) {
            auto frame = ring.pop();
            if (frame) {
                popCount.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    producer.join();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    getRunning().store(false, std::memory_order_release);
    ring.shutdown();
    consumer.join();

    EXPECT_GT(popCount.load(), 0) << "Consumer starved entirely.";
    EXPECT_LE(popCount.load(), pushCount.load()) << "Consumer popped more frames than existed.";
}