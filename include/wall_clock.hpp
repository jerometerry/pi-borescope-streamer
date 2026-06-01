#pragma once

#include <chrono>
#include "clock.hpp"

/** 
 * @brief A clock that provides the current wall time
 */
class WallClock : public Clock {
public:
    /** 
     * @brief Get the current wall time
     * @return The current wall time
     */
    std::chrono::steady_clock::time_point now() const noexcept override {
        return std::chrono::steady_clock::now();
    }
};