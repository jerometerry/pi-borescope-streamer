#include <benchmark/benchmark.h>
#include <cstdint>
#include <thread>
#include "disruptor.hpp" // Ensure you rename or toggle implementations here

struct alignas(disruptor::cache_line_size) Event {
    int64_t id;
};

// Sustained Stream Throughput Benchmark
static void BM_Disruptor_SustainedStream(benchmark::State& state) {
    constexpr size_t BufferSize = 65536;
    disruptor::Disruptor<Event, BufferSize> pipeline;

    std::atomic<bool> running{true};
    std::atomic<int64_t> total_processed{0};

    // Spin up a long-lived consumer thread for the duration of this state run
    std::jthread consumer([&pipeline, &running, &total_processed]() {
        int64_t next_read = 0;
        while (running || pipeline.getHighestPublished() >= next_read) {
            // High frequency polling checks
            int64_t available = pipeline.getHighestPublished();
            
            if (next_read <= available) {
                // Batch drain optimization loop
                while (next_read <= available) {
                    Event& event = pipeline.getBySequence(next_read);
                    benchmark::DoNotOptimize(event.id);
                    next_read++;
                    total_processed.fetch_add(1, std::memory_order_relaxed);
                }
                pipeline.markConsumed(next_read - 1);
            } else {
                // Mimic natural ultra-low latency spin backoff
                #if defined(__x86_64__) || defined(_M_X64)
                    #if defined(_MSC_VER)
                        _mm_pause();
                    #else
                        asm volatile("pause" ::: "memory");
                    #endif
                #else
                    std::this_thread::yield();
                #endif
            }
        }
    });

    // Main Test execution loop
    int64_t current_seq = 0;
    for (auto _ : state) {
        int64_t seq = pipeline.claim();
        Event& event = pipeline.getBySequence(seq);
        event.id = current_seq++;
        pipeline.publish(seq);
    }

    // Clean up termination signal safely
    running = false;
    
    // Set explicit metrics tracking item throughput per second
    state.SetItemsProcessed(state.iterations());
}

// 1. High-contention sustained stream
BENCHMARK(BM_Disruptor_SustainedStream)
    ->Unit(benchmark::kMicrosecond)
    ->UseRealTime();


// 2. Heavy Contention / Backpressure Benchmark
// Exposes if the claim loop blocks or burns CPU cycles inefficiently
static void BM_Disruptor_BackpressureBursts(benchmark::State& state) {
    constexpr size_t BufferSize = 1024; // Small buffer forces extreme wrapping/polling conditions
    disruptor::Disruptor<Event, BufferSize> pipeline;
    
    const int64_t burst_size = state.range(0);
    std::atomic<bool> stop_consumer{false};

    std::jthread consumer([&pipeline, &stop_consumer, burst_size]() {
        int64_t next_read = 0;
        while (!stop_consumer.load(std::memory_order_relaxed)) {
            int64_t available = pipeline.getHighestPublished();
            if (next_read <= available) {
                // Artificially delay consumer slightly to force producer to experience wrap bounds
                if (next_read % burst_size == 0) {
                    std::this_thread::sleep_for(std::chrono::nanoseconds(50));
                }
                while (next_read <= available) {
                    Event& event = pipeline.getBySequence(next_read);
                    benchmark::DoNotOptimize(event.id);
                    next_read++;
                }
                pipeline.markConsumed(next_read - 1);
            }
        }
    });

    for (auto _ : state) {
        for (int64_t i = 0; i < burst_size; ++i) {
            int64_t seq = pipeline.claim();
            pipeline.getBySequence(seq).id = i;
            pipeline.publish(seq);
        }
    }

    stop_consumer = true;
    state.SetItemsProcessed(state.iterations() * burst_size);
}

BENCHMARK(BM_Disruptor_BackpressureBursts)
    ->Arg(500)
    ->Arg(5000)
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();

BENCHMARK_MAIN();
