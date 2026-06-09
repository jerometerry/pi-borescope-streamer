#include <atomic>
#include <array>
#include <bit>
#include <concepts>
#include <iostream>
#include <new>
#include <thread>
#include <cstdint>

namespace disruptor {

#ifdef __cpp_lib_hardware_interference_size
    inline constexpr size_t cache_line_size = 
        (std::hardware_destructive_interference_size <= 64) 
        ? std::hardware_destructive_interference_size : 64;
#else
    inline constexpr size_t cache_line_size = 64;
#endif

    struct alignas(cache_line_size) Sequence {
        std::atomic<int64_t> value{-1};

        [[nodiscard]] int64_t wait_until_greater_or_equal(int64_t target) const noexcept {
            int64_t current = value.load(std::memory_order_acquire);
            while (current < target) {
                value.wait(current, std::memory_order_acquire);
                current = value.load(std::memory_order_acquire);
            }
            return current;
        }

        void publish(int64_t new_value) noexcept {
            value.store(new_value, std::memory_order_release);
            value.notify_all(); // Wakes up std::atomic::wait
        }
    };

    template <typename T, size_t Capacity>
    requires (std::has_single_bit(Capacity))
    class RingBuffer {
    private:
        static constexpr size_t mask = Capacity - 1;
        alignas(cache_line_size) std::array<T, Capacity> data_{};

    public:
        [[nodiscard]] T& operator[](int64_t sequence) noexcept {
            return data_[sequence & mask]; // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
        }
        
        [[nodiscard]] static constexpr size_t capacity() noexcept {
            return Capacity;
        }
    };

    template <typename T, size_t Capacity>
    class SPSCDisruptor {
    private:
        alignas(cache_line_size) RingBuffer<T, Capacity> buffer_;

        alignas(cache_line_size) Sequence claim_sequence_;
        alignas(cache_line_size) Sequence published_sequence_;
        alignas(cache_line_size) Sequence consumer_sequence_;

    public:
        [[nodiscard]] int64_t claim() noexcept {
            int64_t next_sequence = claim_sequence_.value.load(std::memory_order_relaxed) + 1;
            int64_t wrap_point = next_sequence - buffer_.capacity();

            while (consumer_sequence_.value.load(std::memory_order_acquire) < wrap_point) {
                std::this_thread::yield(); 
            }

            claim_sequence_.value.store(next_sequence, std::memory_order_relaxed);
            return next_sequence;
        }

        T& get_by_sequence(int64_t sequence) noexcept {
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