#include <benchmark/benchmark.h>
#include <fstream>
#include <vector>
#include "buffer.hpp"
#include "buffer_pool.hpp"
#include "buffer_ptr.hpp"
#include "mjpeg_stream.hpp"
#include "mjpeg_frame_queue.hpp"

// A helper to load a raw MJPEG frame from disk
std::vector<uint8_t> load_sample_frame(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}

static void BM_Pipeline_Throughput(benchmark::State& state) {
    // 1. Setup the pipeline
    auto pool = BufferPool::create();
    MjpegFrameQueue queue;
    MjpegStream stream(pool, [&queue](const BufferPtr& frame) {
        queue.push(frame);
    });

    std::vector<uint8_t> frame_data = load_sample_frame("test_data/sample.jpg");

    for (auto _ : state) {
        stream.send(frame_data);

        // Mocking the Server/Consumer popping the frame
        uint32_t id{0};
        auto frame = queue.pop(id);
        
        benchmark::DoNotOptimize(frame);
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_Pipeline_Throughput)
	->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();