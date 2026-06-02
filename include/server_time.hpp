#pragma once

#include <chrono>
class Clock;

/**
 * @brief The central timekeeper used to track events and measure elapsed durations.
 * @details Rather than having every class calculate time differences manually with 
 * complex `std::chrono` math, this class provides a simple, clean interface. 
 * 
 * When the server boots up, it hands this timekeeper to classes like the 
 * `HardwareButtonManager` so they can easily ask, "How many milliseconds exactly 
 * have passed since the user first pressed the button?"
 */
class ServerTime {
public:
     /**
      * @brief Wind up the timekeeper.
      * @param clock A reference to the clock mechanism driving this timekeeper (real or test).
      * @param start The exact moment the server was powered on.
      */
    explicit ServerTime(const Clock& clock, std::chrono::steady_clock::time_point start);

    /**
     * @brief Check how long the server has been running.
     * @return The total elapsed time in milliseconds since startup.
     */
    [[nodiscard]] long long get() const;

    /**
     * @brief Calculate the exact gap between two specific moments in time.
     * @param start The beginning moment.
     * @param end The ending moment.
     * @return The difference in milliseconds.
     */
    [[nodiscard]] static long long getElapsedMilliseconds(
        std::chrono::steady_clock::time_point start, 
        std::chrono::steady_clock::time_point end);

    /**
     * @brief Ask the underlying clock what time it is right now.
     * @return The current time point.
     */
    [[nodiscard]] std::chrono::steady_clock::time_point now() const;

private:
    /**
     * @brief The recorded moment the server first started.
     */
    std::chrono::steady_clock::time_point start_;

    /**
     * @brief The clock mechanism driving the timekeeper's measurements.
     */
    const Clock* clock_;
};