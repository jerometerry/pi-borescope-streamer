#pragma once

#include <chrono>
class Clock;

/**
 * @brief Class representing the time for the server
 */
class ServerTime {
public:
     /** @brief Construct a new server time instance
      * @param clock A reference to the clock interface (guarantees non-null at creation)
      * @param start The start time for the server
      */
    explicit ServerTime(const Clock& clock, std::chrono::steady_clock::time_point start);

    /** @brief Get the elapsed time in milliseconds since the server started
     * @return Elapsed time in milliseconds
     */
    [[nodiscard]] long long get() const;

    /** @brief Get the elapsed time in milliseconds between two points in time
     * @param start The start time
     * @param end The end time
     * @return Elapsed time in milliseconds
     */
    [[nodiscard]] static long long getElapsedMilliseconds(
        std::chrono::steady_clock::time_point start, 
        std::chrono::steady_clock::time_point end);

    /** @brief Get the current time
     * @return The current time
     */
    [[nodiscard]] std::chrono::steady_clock::time_point now() const;

private:
    /** @brief The start time for the server
     */
    std::chrono::steady_clock::time_point start_;

    /** @brief A reference to the clock interface
     */
    const Clock* clock_;
};