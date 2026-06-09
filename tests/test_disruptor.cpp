#include <gtest/gtest.h>
#include <atomic>
#include <cstdint>
#include <thread>
#include "disruptor.hpp"

struct Event {
    int64_t id;
    double price;
};

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