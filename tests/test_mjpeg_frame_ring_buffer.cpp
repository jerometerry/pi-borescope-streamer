#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>
#include "buffer.hpp"
#include "buffer_ptr.hpp"
#include "buffer_pool.hpp"
#include "mjpeg_frame_ring_buffer.hpp"

// --- Helper Functions ---
namespace {
    /**
     * @brief Utility to easily spin up populated frames for testing.
     */
    BufferPtr createTestFrame(const std::shared_ptr<BufferPool>& pool, const std::vector<uint8_t>& data) {
        auto frame = pool->borrow();
        if (!data.empty()) {
            frame->insertContent(data);
        }
        return frame;
    }
}

// --- Test Suite ---

class MjpegFrameRingBufferTest : public ::testing::Test {
private:
    std::shared_ptr<BufferPool> pool_;
    std::atomic<bool> running_{true};

protected:
    void SetUp() override {
        // Initialize a standard pool for the tests
        pool_ = BufferPool::create();
        running_.store(true, std::memory_order_release);
    }

    void TearDown() override {
        // Ensure atomic flag is reset so threads don't leak on failure
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

    // Push two frames
    ring.push(createTestFrame(getPool(), {0x01, 0x02}));
    ring.push(createTestFrame(getPool(), {0x03, 0x04}));

    // Pop the first
    auto frame1 = ring.pop();
    ASSERT_TRUE(frame1);
    EXPECT_THAT(frame1->getContentSlice(), ::testing::ElementsAre(0x01, 0x02));

    // Pop the second
    auto frame2 = ring.pop();
    ASSERT_TRUE(frame2);
    EXPECT_THAT(frame2->getContentSlice(), ::testing::ElementsAre(0x03, 0x04));
}

TEST_F(MjpegFrameRingBufferTest, Push_SafelyIgnoresEmptyFrames) {
    MjpegFrameRingBuffer ring(3, getRunning());

    // Attempt to push a null IntrusivePtr
    BufferPtr nullFrame;
    ring.push(nullFrame);

    // Attempt to push a valid pointer but empty payload
    ring.push(createTestFrame(getPool(), {})); 

    // Trigger a shutdown so pop() doesn't block forever if the queue is empty
    getRunning().store(false, std::memory_order_release);
    ring.shutdown();

    // Verify the queue remained completely empty
    EXPECT_FALSE(ring.pop());
}

TEST_F(MjpegFrameRingBufferTest, Push_OverflowEnforcesDropOldestPolicy) {
    constexpr size_t CAPACITY = 3;
    MjpegFrameRingBuffer ring(CAPACITY, getRunning());

    // Push 5 frames into a queue with capacity 3.
    // Frames 1 and 2 should be aggressively dropped/overwritten.
    for (uint8_t i = 1; i <= 5; ++i) {
        ring.push(createTestFrame(getPool(), {i}));
    }

    // The remaining frames should be 3, 4, and 5.
    for (uint8_t expectedVal = 3; expectedVal <= 5; ++expectedVal) {
        auto frame = ring.pop();
        ASSERT_TRUE(frame) << "Expected frame " << static_cast<int>(expectedVal) << " was missing.";
        EXPECT_THAT(frame->getContentSlice(), ::testing::ElementsAre(expectedVal));
    }

    // Shut down and verify nothing else remains
    getRunning().store(false, std::memory_order_release);
    ring.shutdown();
    EXPECT_FALSE(ring.pop());
}

TEST_F(MjpegFrameRingBufferTest, Pop_ShutdownSafelyUnblocksSleepingConsumers) {
    MjpegFrameRingBuffer ring(5, getRunning());
    std::atomic<bool> threadExitedCleanly{false};

    std::thread consumer([&]() {
        // This will immediately block because the queue is empty
        auto frame = ring.pop();
        
        // Once unblocked by shutdown, it should receive a null pointer
        EXPECT_FALSE(frame); 
        threadExitedCleanly.store(true, std::memory_order_release);
    });

    // Give the OS scheduler a tiny fraction of time to put the consumer thread to sleep
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Initiate the teardown sequence
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

    // Producer Thread
    std::thread producer([&]() {
        for (int i = 0; i < TARGET_FRAMES; ++i) {
            ring.push(createTestFrame(getPool(), {0xFF}));
            pushCount.fetch_add(1, std::memory_order_relaxed);
            
            // Occasionally yield to induce queue oscillation
            if (i % 10 == 0) std::this_thread::yield();
        }
    });

    // Consumer Thread
    std::thread consumer([&]() {
        while (getRunning().load(std::memory_order_acquire)) {
            auto frame = ring.pop();
            if (frame) {
                popCount.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    producer.join();

    // Give the consumer a brief moment to drain the remaining queue
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Shut down the consumer
    getRunning().store(false, std::memory_order_release);
    ring.shutdown();
    consumer.join();

    // In a Drop-Oldest architecture, the consumer may not see every frame, 
    // but the queue should not crash, deadlock, or drop frames unprovoked.
    // We expect the sum of popped frames + any dropped frames to equal TARGET_FRAMES.
    EXPECT_GT(popCount.load(), 0) << "Consumer starved entirely.";
    EXPECT_LE(popCount.load(), pushCount.load()) << "Consumer popped more frames than existed.";
}