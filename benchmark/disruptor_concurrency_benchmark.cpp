#include <benchmark/benchmark.h>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>
#include "disruptor.hpp"

struct alignas(disruptor::cache_line_size) Event {
    int64_t id{0};
};

template <typename WaitStrategy>
static void BM_Disruptor_SustainedStream(benchmark::State& state) {
    constexpr size_t BufferSize = 65536;
    disruptor::Disruptor<Event, BufferSize, WaitStrategy> pipeline;

    std::jthread consumer([&pipeline]() {
        int64_t next_read = 0;
        bool keep_running = true;

        while (keep_running) {
            int64_t available = pipeline.waitFor(next_read);

            while (next_read <= available) {
                Event& event = pipeline.getBySequence(next_read);

                if (event.id == -1) {
                    keep_running = false;
                    break;
                }

                benchmark::DoNotOptimize(event.id);
                next_read++;
            }
            pipeline.markConsumed(next_read - 1);
        }
    });

    int64_t current_seq = 0;
    for (auto _ : state) {
        int64_t seq = pipeline.claim();
        Event& event = pipeline.getBySequence(seq);
        event.id = current_seq++;
        pipeline.publish(seq);
    }

    int64_t shutdown_seq = pipeline.claim();
    pipeline.getBySequence(shutdown_seq).id = -1;
    pipeline.publish(shutdown_seq);

    state.SetItemsProcessed(state.iterations());
}

template <typename WaitStrategy>
static void BM_Disruptor_BackpressureBursts(benchmark::State& state) {
    constexpr size_t BufferSize = 1024;
    disruptor::Disruptor<Event, BufferSize, WaitStrategy> pipeline;
    
    const int64_t burst_size = state.range(0);

    std::jthread consumer([&pipeline, burst_size]() {
        int64_t next_read = 0;
        bool keep_running = true;

        while (keep_running) {
            int64_t available = pipeline.waitFor(next_read);

            if (next_read % burst_size == 0) {
                std::this_thread::sleep_for(std::chrono::nanoseconds(50));
            }

            while (next_read <= available) {
                Event& event = pipeline.getBySequence(next_read);
                if (event.id == -1) {
                    keep_running = false;
                    break;
                }
                benchmark::DoNotOptimize(event.id);
                next_read++;
            }
            pipeline.markConsumed(next_read - 1);
        }
    });

    for (auto _ : state) {
        for (int64_t i = 0; i < burst_size; ++i) {
            int64_t seq = pipeline.claim();
            pipeline.getBySequence(seq).id = i;
            pipeline.publish(seq);
        }
    }

    int64_t shutdown_seq = pipeline.claim();
    pipeline.getBySequence(shutdown_seq).id = -1;
    pipeline.publish(shutdown_seq);

    state.SetItemsProcessed(state.iterations() * burst_size);
}

BENCHMARK(BM_Disruptor_SustainedStream<disruptor::YieldingWaitStrategy>)
    ->Name("BM_Disruptor_SustainedStream/Yielding")
    ->Unit(benchmark::kMicrosecond)
    ->UseRealTime();

BENCHMARK(BM_Disruptor_SustainedStream<disruptor::BlockingWaitStrategy>)
    ->Name("BM_Disruptor_SustainedStream/Blocking")
    ->Unit(benchmark::kMicrosecond)
    ->UseRealTime();

BENCHMARK(BM_Disruptor_BackpressureBursts<disruptor::YieldingWaitStrategy>)
    ->Name("BM_Disruptor_BackpressureBursts/Yielding")
    ->Arg(500)
    ->Arg(5000)
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();

BENCHMARK(BM_Disruptor_BackpressureBursts<disruptor::BlockingWaitStrategy>)
    ->Name("BM_Disruptor_BackpressureBursts/Blocking")
    ->Arg(500)
    ->Arg(5000)
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();

BENCHMARK_MAIN();
