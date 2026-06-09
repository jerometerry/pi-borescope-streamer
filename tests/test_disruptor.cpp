#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include "disruptor.hpp"

using namespace disruptor;

struct Event {
    int64_t id;
    double price;
};

// 1. Test basic FIFO order
TEST(DisruptorCorrectness, MaintainsFifoOrder) {
    SPSCDisruptor<Event, 4> pipeline;

    for (int64_t i = 0; i < 3; ++i) {
        int64_t seq = pipeline.claim();
        pipeline.get_by_sequence(seq).id = i;
        pipeline.publish(seq);
    }

    for (int64_t i = 0; i < 3; ++i) {
        int64_t available = pipeline.wait_for(i);
        EXPECT_GE(available, i);
        EXPECT_EQ(pipeline.get_by_sequence(i).id, i);
        pipeline.mark_consumed(i);
    }
}

// 2. Test boundary wraparound
TEST(DisruptorCorrectness, HandlesBufferWraparound) {
    // Capacity 4 means indices 0, 1, 2, 3
    SPSCDisruptor<Event, 4> pipeline;

    // Push 6 items to force a wrap
    for (int64_t i = 0; i < 6; ++i) {
        [[maybe_unused]] int64_t seq = pipeline.claim();
        pipeline.get_by_sequence(seq).id = i;
        pipeline.publish(seq);
        
        // Consume immediately to keep the producer moving
        int64_t available = pipeline.wait_for(seq);
        EXPECT_GE(available, seq);
        pipeline.mark_consumed(seq);
    }
    
    // Verify the last published value is still correct despite the wrap
    EXPECT_EQ(pipeline.get_by_sequence(5).id, 5);
}

// 3. Test Backpressure (The Producer should block if consumer stops)
TEST(DisruptorCorrectness, ProducerBlocksWhenFull) {
    SPSCDisruptor<Event, 4> pipeline;

    // Fill the buffer to capacity (4 slots)
    for (int i = 0; i < 4; ++i) {
        int64_t seq = pipeline.claim();
        pipeline.publish(seq);
    }

    // Attempting to claim the 5th slot should block.
    // We test this by running a separate thread and checking if it's still alive after a delay.
    std::atomic<bool> claimed{false};
    std::jthread producer([&pipeline, &claimed]() {
        int64_t available = pipeline.claim(); // This should hang
        EXPECT_GE(available, 0);
        claimed = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // If 'claimed' is still false, the producer correctly blocked
    EXPECT_FALSE(claimed.load());

    // Release one slot so the producer can finish
    pipeline.mark_consumed(0);
    
    // Wait briefly for the thread to unblock
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(claimed.load());
}

TEST(DisruptorTest, ConcurrentStressTestProcessesAllEventsWithoutDataLoss) {
    constexpr int64_t TOTAL_EVENTS = 10'000'000;
    
    // Gauss's formula: Sum of 0 to N-1
    constexpr int64_t EXPECTED_EVENT_ID_SUM = (TOTAL_EVENTS * (TOTAL_EVENTS - 1)) / 2;
    
    disruptor::SPSCDisruptor<Event, 65536> pipeline; 
    std::atomic<int64_t> actual_id_sum{0};

    // Consumer Thread
    std::jthread consumer([&pipeline, &actual_id_sum]() {
        int64_t next_read = 0;
        int64_t local_sum = 0;
        
        while (next_read < TOTAL_EVENTS) {
            int64_t available = pipeline.wait_for(next_read);
            
            while (next_read <= available && next_read < TOTAL_EVENTS) {
                Event& event = pipeline.get_by_sequence(next_read);
                local_sum += event.id;
                next_read++;
            }
            
            pipeline.mark_consumed(next_read - 1);
        }
        
        // Export the result back to the main thread
        actual_id_sum.store(local_sum, std::memory_order_release);
    });

    // Producer Thread (Main Thread)
    for (int64_t i = 0; i < TOTAL_EVENTS; ++i) {
        int64_t seq = pipeline.claim();
        
        Event& event = pipeline.get_by_sequence(seq);
        event.id = i;
        event.price = 100.0 + (i % 10);
        
        pipeline.publish(seq);
    }

    // Force the main thread to wait for the consumer to finish its batching
    // before evaluating the final assertion.
    if (consumer.joinable()) {
        consumer.join();
    }
    
    // The final source of truth
    EXPECT_EQ(actual_id_sum.load(std::memory_order_acquire), EXPECTED_EVENT_ID_SUM)
        << "The consumer missed events or read corrupted data during the stress test.";
}