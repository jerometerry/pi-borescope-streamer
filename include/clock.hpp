#pragma once

#include <chrono>

/**
 * @brief Abstraction to break hard dependency on system clock, to testing time based code. 
 */
class Clock {
public:
    virtual ~Clock() = default;
    virtual std::chrono::steady_clock::time_point now() const noexcept = 0;
};