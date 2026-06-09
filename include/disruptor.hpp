#include <atomic>
#include <array>
#include <bit>
#include <concepts>
#include <new>
#include <thread>
#include <cstdint>

namespace disruptor {

    // 1. Cross-Platform Cache Line Sizing
    // Prevents False Sharing across CPU interconnects
#ifdef __cpp_lib_hardware_interference_size
    inline constexpr size_t cache_line_size = std::hardware_destructive_interference_size;
#else
    inline constexpr size_t cache_line_size = 64;
#endif

    // 2. The Padded Odometer (Sequence)
    // Ensures the atomic counter sits completely alone on its CPU cache line.
    struct alignas(cache_line_size) Sequence {
        std::atomic<int64_t> value{-1};

        // C++20 Atomic Wait eliminates condition variables
        [[nodiscard]] int64_t wait_until_greater_or_equal(int64_t target) const noexcept {
            int64_t current = value.load(std::memory_order_acquire);
            while (current < target) {
                // Suspends the thread at the OS level if value == current.
                // Wakes up instantly (no mutex lock required) when modified.
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

    // 3. The "Dumb" Ring Buffer
    // Contains zero synchronization logic. Pure pre-allocated contiguous memory.
    template <typename T, size_t Capacity>
    requires (std::has_single_bit(Capacity)) // C++20 constraint: Must be power of 2
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

    // 4. The Single-Producer Pipeline
    template <typename T, size_t Capacity>
    class SPSCDisruptor {
    private:
        RingBuffer<T, Capacity> buffer_;
        
        // The Producer's Bookmark (Where are we writing?)
        Sequence claim_sequence_;     
        // The Producer's Published Line (What is ready to read?)
        Sequence published_sequence_; 
        // The Consumer's Bookmark (What have we finished reading?)
        Sequence consumer_sequence_;  

    public:
        // --- PRODUCER API ---

        [[nodiscard]] int64_t claim() noexcept {
            int64_t next_sequence = claim_sequence_.value.load(std::memory_order_relaxed) + 1;
            int64_t wrap_point = next_sequence - buffer_.capacity();

            // Backpressure: If the wrap point catches up to the consumer, we must wait.
            // This is the ONLY time the producer is blocked.
            while (consumer_sequence_.value.load(std::memory_order_acquire) < wrap_point) {
                // Spin-wait or yield. The consumer is lagging.
                std::this_thread::yield(); 
            }

            // Claim the sequence
            claim_sequence_.value.store(next_sequence, std::memory_order_relaxed);
            return next_sequence;
        }

        T& get_by_sequence(int64_t sequence) noexcept {
            return buffer_[sequence];
        }

        void publish(int64_t sequence) noexcept {
            // Two-Phase commit: Once data is written, announce it to the consumer.
            published_sequence_.publish(sequence);
        }

        // --- CONSUMER API ---

        [[nodiscard]] int64_t wait_for(int64_t next_sequence) const noexcept {
            // Blocks efficiently without a Mutex until the producer publishes
            return published_sequence_.wait_until_greater_or_equal(next_sequence);
        }

        void mark_consumed(int64_t sequence) noexcept {
            // Release the memory slot back to the producer
            consumer_sequence_.value.store(sequence, std::memory_order_release);
        }
    };
}