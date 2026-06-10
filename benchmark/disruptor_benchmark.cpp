#include <benchmark/benchmark.h>
#include <cstdint>
#include <thread>
#include "disruptor.hpp"

struct alignas(disruptor::cache_line_size) Event {
    int64_t id;
};

static void BM_Disruptor_BatchThroughput(benchmark::State& state) {
    const int64_t items_to_process = state.range(0);
    disruptor::Disruptor<Event, 65536> pipeline;

    std::jthread consumer([&pipeline]() {
        int64_t next_read = 0;
        while (true) {
            int64_t available = pipeline.waitFor(next_read);

            while (next_read <= available) {
                Event& event = pipeline.getBySequence(next_read);
                benchmark::DoNotOptimize(event.id);

                if (event.id == -1) {
                    pipeline.markConsumed(next_read); 
                    return; 
                }
                next_read++;
            }
            pipeline.markConsumed(next_read - 1);
        }
    });

    for (auto _ : state) {
        for (int64_t i = 0; i < items_to_process; ++i) {
            int64_t seq = pipeline.claim();
            Event& event = pipeline.getBySequence(seq);
            event.id = i;
            pipeline.publish(seq);
        }
    }

    int64_t seq = pipeline.claim();
    pipeline.getBySequence(seq).id = -1;
    pipeline.publish(seq);

    state.SetItemsProcessed(state.iterations() * items_to_process);
}

BENCHMARK(BM_Disruptor_BatchThroughput)
    ->RangeMultiplier(10)
    ->Range(100, 10000)
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();

BENCHMARK_MAIN();