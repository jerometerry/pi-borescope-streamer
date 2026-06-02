#pragma once

#include <chrono>

/**
 * @brief The master blueprint for how the software measures time.
 * @details In simple hardware projects, you usually just ask the system for the real time. 
 * But in a complex system, hardcoding real time makes the code impossible to test. If you 
 * want to test what happens when a user holds a button for 5 seconds, you don't want your 
 * automated software tests to actually pause for 5 seconds. 
 * 
 * By forcing every part of the system to read from a `Clock` interface instead of the 
 * system's actual hardware time, we can create fake "Test Clocks" that instantly fast-forward 
 * time, guaranteeing our button debouncing and network timeouts work perfectly without waiting.
 */
class Clock {
public:
    /**
     * @brief Safely scrap the clock.
     */
    virtual ~Clock() = default;

    /**
     * @brief Ask the clock what time it currently is.
     * @return The current time point according to this specific clock.
     */
    virtual std::chrono::steady_clock::time_point now() const noexcept = 0;
};