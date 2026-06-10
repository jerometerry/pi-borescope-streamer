#pragma once

#if defined(__x86_64__) || defined (_M_X64)
#if defined(_MSC_VER)
#include <immintrin.h>
#endif
#endif

#include <atomic>
#include <array>
#include <bit>
#include <concepts>
#include <cstdint>
#include <new>
#include <optional>
#include <thread>

namespace disruptor {

#if defined(__cpp_lib_hardware_interference_size) && !defined(__arm__) && !defined(__aarch64__)
    inline constexpr size_t cache_line_size = std::hardware_destructive_interference_size;
#else
    inline constexpr size_t cache_line_size = 64;
#endif

    struct alignas(cache_line_size) Sequence {
        std::atomic<int64_t> value{-1};

        [[nodiscard]] int64_t waitUntilGreaterOrEqual(int64_t target) const noexcept {
            int64_t current = value.load(std::memory_order_acquire);
            
            // Hybrid Spin-Wait Strategy: Low-latency spinning first
            uint32_t spinCount = 0;
            while (current < target && spinCount < 2000) {
                #if defined(__x86_64__) || defined (_M_X64)
                    #if defined(_MSC_VER)
                        _mm_pause();
                    #else
                        asm volatile("pause" ::: "memory");
                    #endif
                #elif defined(__aarch64__) || defined(_M_ARM64)
                    asm volatile("yield" ::: "memory");
                #else
                    std::this_thread::yield();
                #endif
                current = value.load(std::memory_order_relaxed);
                spinCount++;
            }

            // Fallback to OS-assisted sleep if the producer/consumer is heavily delayed
            current = value.load(std::memory_order_acquire);
            while (current < target) {
                value.wait(current, std::memory_order_acquire);
                current = value.load(std::memory_order_acquire);
            }
            return current;
        }

        void publish(int64_t newValue) noexcept {
            value.store(newValue, std::memory_order_release);
            value.notify_all(); // Wakes up OS thread if it fell asleep
        }
    };

    template <typename T, size_t Capacity>
    requires (std::has_single_bit(Capacity))
    class alignas(cache_line_size) RingBuffer {
    private:
        static constexpr size_t mask = Capacity - 1;
        std::array<T, Capacity> data_{};

    public:
        [[nodiscard]] T& operator[](int64_t sequence) noexcept {
            return data_[sequence & mask]; // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
        }
        
        [[nodiscard]] static constexpr size_t capacity() noexcept {
            return Capacity;
        }
    };

    template <typename T, size_t Capacity>
    class Disruptor {
    private:
        alignas(cache_line_size) RingBuffer<T, Capacity> buffer_;
        alignas(cache_line_size) Sequence publishedSequence_;
        alignas(cache_line_size) Sequence consumerSequence_;

        alignas(cache_line_size) int64_t producerSequence_{-1};
        alignas(cache_line_size) int64_t cachedConsumerSequence_{-1};

    public:
        void preAllocate(const int64_t slotSize) {
            for (size_t i = 0; i < Capacity; i++) {
                auto slot = getBySequence(i);
                slot.preAllocate(slotSize);
            }
        }

        [[nodiscard]] int64_t claim() noexcept {
            int64_t nextSequence = producerSequence_ + 1;
            int64_t wrapPoint = nextSequence - buffer_.capacity();

            if (cachedConsumerSequence_ < wrapPoint) {
                while ((cachedConsumerSequence_ = consumerSequence_.value.load(std::memory_order_acquire)) < wrapPoint) {
                    #if defined(__x86_64__) || defined (_M_X64)
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

            producerSequence_ = nextSequence;
            return nextSequence;
        }

        [[nodiscard]] std::optional<int64_t> tryClaim() noexcept {
            int64_t nextSequence = producerSequence_ + 1;
            int64_t wrapPoint = nextSequence - buffer_.capacity();

            if (cachedConsumerSequence_ < wrapPoint) {
                cachedConsumerSequence_ = consumerSequence_.value.load(std::memory_order_relaxed);

                if (cachedConsumerSequence_ < wrapPoint) {
                    return std::nullopt;
                }
            }

            producerSequence_ = nextSequence;
            return nextSequence;
        }

        [[nodiscard]] T& getBySequence(int64_t sequence) noexcept {
            return buffer_[sequence];
        }

        void publish(int64_t sequence) noexcept {
            publishedSequence_.publish(sequence);
        }

        [[nodiscard]] int64_t waitFor(int64_t nextSequence) const noexcept {
            return publishedSequence_.waitUntilGreaterOrEqual(nextSequence);
        }

        void markConsumed(int64_t sequence) noexcept {
            consumerSequence_.value.store(sequence, std::memory_order_release);
        }

        [[nodiscard]] int64_t getHighestPublished() const noexcept {
            return publishedSequence_.value.load(std::memory_order_acquire);
        }
    };
}
