#include <benchmark/benchmark.h>
#include <thread>
#include <atomic>
#include "disruptor.hpp"

struct Event {
    int64_t id;
    double price;
};

static void BM_Disruptor_BatchThroughput(benchmark::State& state) {
    // Determine how many events to push per benchmark iteration
    const int64_t items_to_process = state.range(0);
    
    disruptor::SPSCDisruptor<Event, 65536> pipeline;

    // Google Benchmark will run this loop as many times as it needs to 
    // get a statistically significant result.
    for (auto _ : state) {
        // Pause the timer so thread creation overhead doesn't ruin the numbers
        state.PauseTiming(); 

        std::jthread consumer([&pipeline, items_to_process]() {
            int64_t next_read = 0;
            while (next_read < items_to_process) {
                int64_t available = pipeline.wait_for(next_read);
                
                while (next_read <= available && next_read < items_to_process) {
                    Event& event = pipeline.get_by_sequence(next_read);
                    
                    // Hardware Optimization Barrier!
                    // Forces the CPU to actually read the memory without 
                    // the compiler deleting the loop via Dead Code Elimination.
                    benchmark::DoNotOptimize(event);
                    
                    next_read++;
                }
                pipeline.mark_consumed(next_read - 1);
            }
        });

        // Start the clock!
        state.ResumeTiming();

        for (int64_t i = 0; i < items_to_process; ++i) {
            int64_t seq = pipeline.claim();
            pipeline.get_by_sequence(seq).id = i;
            pipeline.publish(seq);
        }

        // Wait for the consumer to finish this batch before ending the timer
        if (consumer.joinable()) {
            consumer.join();
        }
    }

    // Tell Google Benchmark how much data we actually moved
    // so it can calculate "Items Per Second"
    state.SetItemsProcessed(state.iterations() * items_to_process);
}

// Run the benchmark with 1 Million events, and output the time in milliseconds
BENCHMARK(BM_Disruptor_BatchThroughput)
    ->Arg(1'000'000)
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();