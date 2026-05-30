#pragma once

#include "clock.hpp"
#include <chrono>

/**
 * @brief Class representing the time for the server
 */
class ServerTime {
public:
    /** * @brief Construct a new server time instance
     * @param clock A reference to the clock interface (guarantees non-null at creation)
     * @param start The start time for the server
     */
    explicit ServerTime(const Clock& clock, std::chrono::steady_clock::time_point start);

    [[nodiscard]] long long get() const;

    [[nodiscard]] static long long getElapsedMilliseconds(
        std::chrono::steady_clock::time_point start, 
        std::chrono::steady_clock::time_point end);

    [[nodiscard]] std::chrono::steady_clock::time_point now() const;

private:
    std::chrono::steady_clock::time_point start_;
    const Clock* clock_;
};