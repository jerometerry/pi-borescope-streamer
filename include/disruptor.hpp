#include <atomic>
#include <array>
#include <bit>
#include <concepts>
#include <cstdint>
#include <new>
#include <thread>

namespace disruptor {

#if defined(__cpp_lib_hardware_interference_size) && !defined(__arm__) && !defined(__aarch64__)
    inline constexpr size_t cache_line_size = std::hardware_destructive_interference_size;
#else
    inline constexpr size_t cache_line_size = 64;
#endif

    struct alignas(cache_line_size) Sequence {
        std::atomic<int64_t> value{-1};

        [[nodiscard]] int64_t wait_until_greater_or_equal(int64_t target) const noexcept {
            int64_t current = value.load(std::memory_order_acquire);
            
            // Hybrid Spin-Wait Strategy: Low-latency spinning first
            uint32_t spin_count = 0;
            while (current < target && spin_count < 2000) {
                std::atomic_ some_fence_or_hint = [](){
                    #if defined(__x86_64__) || defined(_M_X64)
                    asm volatile("pause" ::: "memory");
                    #elif defined(__aarch64__) || defined(_M_ARM64)
                    asm volatile("yield" ::: "memory");
                    #endif
                };
                some_fence_or_hint();
                current = value.load(std::memory_order_relaxed);
                spin_count++;
            }

            // Fallback to OS-assisted sleep if the producer/consumer is heavily delayed
            current = value.load(std::memory_order_acquire);
            while (current < target) {
                value.wait(current, std::memory_order_acquire);
                current = value.load(std::memory_order_acquire);
            }
            return current;
        }

        void publish(int64_t new_value) noexcept {
            value.store(new_value, std::memory_order_release);
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
            return data_[sequence & mask]; 
        }
        
        [[nodiscard]] static constexpr size_t capacity() noexcept {
            return Capacity;
        }
    };

    template <typename T, size_t Capacity>
    class SPSCDisruptor {
    private:
        RingBuffer<T, Capacity> buffer_;
        Sequence claim_sequence_;
        Sequence published_sequence_;
        Sequence consumer_sequence_;

    public:
        [[nodiscard]] int64_t claim() noexcept {
            int64_t next_sequence = claim_sequence_.value.load(std::memory_order_relaxed) + 1;
            int64_t wrap_point = next_sequence - buffer_.capacity();

            while (consumer_sequence_.value.load(std::memory_order_acquire) < wrap_point) {
                std::this_thread::yield(); 
            }

            claim_sequence_.value.store(next_sequence, std::memory_order_release);
            return next_sequence;
        }

        [[nodiscard]] T& get_by_sequence(int64_t sequence) noexcept {
            return buffer_[sequence];
        }

        void publish(int64_t sequence) noexcept {
            published_sequence_.publish(sequence);
        }

        [[nodiscard]] int64_t wait_for(int64_t next_sequence) const noexcept {
            return published_sequence_.wait_until_greater_or_equal(next_sequence);
        }

        void mark_consumed(int64_t sequence) noexcept {
            consumer_sequence_.value.store(sequence, std::memory_order_release);
        }
    };
}
