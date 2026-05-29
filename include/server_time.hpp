#pragma once

#include <chrono>
#include <csignal>

/** 
 * @brief Class representing the time for the server
 */
class ServerTime {
public:
    /** 
     * @brief Construct a new server time instance
     * @param serverStartTime The start time for the server
     */
    explicit ServerTime(std::chrono::steady_clock::time_point serverStartTime);

    /** 
     * @brief Get the current time for the server
     * @return The current time in milliseconds
     */
    long long get() const;

private:
    /** 
     * @brief The start time for the server
     */
    std::chrono::steady_clock::time_point serverStartTime;
};
