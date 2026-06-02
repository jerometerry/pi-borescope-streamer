#pragma once

#include <chrono>
#include "clock.hpp"

/**
 * @brief The real-world physical stopwatch.
 * @details While our automated tests use fake, fast-forwarding clocks, this is the 
 * actual hardware clock used when the server is running live on the Raspberry Pi. 
 * It hooks directly into the operating system's `steady_clock` to provide precise, 
 * real-world millisecond measurements that never jump backwards.
 */
class WallClock : public Clock {
public:
    /**
     * @brief Look at the actual physical time.
     * @return The true, current time from the operating system.
     */
    std::chrono::steady_clock::time_point now() const noexcept override {
        return std::chrono::steady_clock::now();
    }
};