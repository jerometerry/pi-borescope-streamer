#include <benchmark/benchmark.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "buffer.hpp"
#include "buffer_pool.hpp"
#include "buffer_ptr.hpp"
#include "constants.hpp"
#include "mjpeg_stream.hpp"
#include "mjpeg_frame_queue.hpp"
#include "zero_allocation_response_builder.hpp"

namespace {
    std::string fileName_ = "./test_data/camera_stream.mjpeg";
}

static std::vector<uint8_t> readBinaryFile(const std::string& fileName) {
    std::ifstream file(fileName, std::ios::binary | std::ios::ate);
    
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: camera_stream.mjpeg");
    }

    std::streamsize size = file.tellg();
    std::vector<uint8_t> buffer = std::vector<uint8_t>(size);

    file.seekg(0, std::ios::beg);

    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        throw std::runtime_error("Error reading data from stream.");   
    }

    return buffer;
}

static std::vector<std::span<const uint8_t>> splitPages(
    const std::vector<uint8_t>& data) {

    std::vector<std::span<const uint8_t>> packets;
    size_t chunkSize = Units::FOUR_KILOBYTES;

    auto reservationSize = (data.size() + chunkSize - 1) / chunkSize;
    packets.reserve(reservationSize);

    for (size_t i = 0; i < data.size(); i += chunkSize) {
        auto endIndex = std::min(
            i + chunkSize, 
            data.size()
        );
        packets.emplace_back(
            data.begin() + i, 
            data.begin() + endIndex
        );
    }

    return packets;
}

static void BM_Pipeline_Throughput(benchmark::State& state) {
    static auto data = readBinaryFile(fileName_);
    static auto pages = splitPages(data);

    BufferPool::BufferPoolArgs args{
        2048,
        2048,
        128
    };
    auto pool = BufferPool::create(args);
    MjpegFrameQueue queue;
    std::atomic<bool> producer_running{true};

    std::jthread consumer([&queue, &producer_running]() {
        while (producer_running.load(std::memory_order_relaxed)) {
            uint32_t frameId{0};
            auto frame = queue.pop(frameId);
            if (frame) {
                auto* buffer = frame.get();
                ZeroAllocationResponseBuilder::build(buffer);
                benchmark::DoNotOptimize(frame);
            }
        }
    });

    MjpegStream stream(pool, [&queue](const BufferPtr& frame) {
        queue.push(frame);
    });

    size_t pageIndex = 0;
    for (auto _ : state) {
        const auto& page = pages[pageIndex % pages.size()];
        stream.send(page);  
        pageIndex++;
    }

    producer_running.store(false, std::memory_order_release);

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * data.size() / (pages.size())); 
}

static void BM_Pipeline_DiskBound(benchmark::State& state) {
    std::ifstream file(fileName_, std::ios::binary);
    if (!file.is_open()) state.SkipWithError("Could not open file.");

    BufferPool::BufferPoolArgs args{
        2048,
        2048,
        128
    };
    auto pool = BufferPool::create(args);
    MjpegFrameQueue queue;
    std::atomic<bool> producer_running{true};

    std::jthread consumer([&queue, &producer_running]() {
        while (producer_running.load(std::memory_order_relaxed)) {
            uint32_t frameId{0};
            auto frame = queue.pop(frameId);
            if (frame) {
                auto* buffer = frame.get();
                ZeroAllocationResponseBuilder::build(buffer);
                benchmark::DoNotOptimize(frame);
            }
        }
    });

    MjpegStream stream(pool, [&queue](const BufferPtr& frame) {
        queue.push(frame);
    });

    std::vector<uint8_t> chunk(4096);
    for (auto _ : state) {
        file.read(reinterpret_cast<char*>(chunk.data()), 4096);

        if (file.eof()) {
            file.clear();
            file.seekg(0, std::ios::beg);
            file.read(reinterpret_cast<char*>(chunk.data()), 4096);
        }

        stream.send(std::span<const uint8_t>(chunk.data(), file.gcount()));
    }

    producer_running = false;
    state.SetBytesProcessed(state.iterations() * 4096);
}

BENCHMARK(BM_Pipeline_Throughput)
    ->Unit(benchmark::kMillisecond)
    ->Threads(1)
    ->Threads(4)
    ->Threads(10)
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Pipeline_DiskBound)
    ->Unit(benchmark::kMillisecond)
    ->Threads(1)
    ->Threads(4)
    ->Threads(10)
    ->Unit(benchmark::kMillisecond);

int main(int argc, char* argv[]) {
    try {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];

            if (arg == "--file" && i + 1 < argc) {
                fileName_ = argv[++i];
            }
        }

        benchmark::Initialize(&argc, argv);
        benchmark::RunSpecifiedBenchmarks();
        benchmark::Shutdown();
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << "[Fatal] Unhandled exception in application core: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}