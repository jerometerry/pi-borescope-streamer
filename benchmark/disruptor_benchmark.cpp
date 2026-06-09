#include <benchmark/benchmark.h>
#include <thread>
#include <atomic>
#include "disruptor.hpp"

struct Event {
    int64_t id;
};

static void BM_Disruptor_BatchThroughput(benchmark::State& state) {
    const int64_t items_to_process = state.range(0);
    disruptor::SPSCDisruptor<Event, 65536> pipeline;
    std::atomic<bool> running{true};

    std::jthread consumer([&pipeline, &running]() {
        int64_t next_read = 0;
        while (running.load(std::memory_order_relaxed)) {
            int64_t available = pipeline.wait_for(next_read);

            while (next_read <= available) {
                const Event& event = pipeline.get_by_sequence(next_read);
                benchmark::DoNotOptimize(&event);

                if (event.id == -1) {
                    return; 
                }
                next_read++;
            }
            pipeline.mark_consumed(next_read - 1);
        }
    });

    for (auto _ : state) {
        for (int64_t i = 0; i < items_to_process; ++i) {
            int64_t seq = pipeline.claim();
            Event& event = pipeline.get_by_sequence(seq);
            event.id = i;
            pipeline.publish(seq);
        }
    }

    running.store(false, std::memory_order_relaxed);

    int64_t seq = pipeline.claim();
    pipeline.get_by_sequence(seq).id = -1; // The Poison Pill
    pipeline.publish(seq);

    state.SetItemsProcessed(state.iterations() * items_to_process);
}

BENCHMARK(BM_Disruptor_BatchThroughput)
    ->Args({100})
    ->Args({1000})
    ->Args({5000})
    ->Args({10000})
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();

BENCHMARK_MAIN();