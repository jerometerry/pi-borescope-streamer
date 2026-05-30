#pragma once
#include <chrono>

class Clock {
public:
    virtual ~Clock() = default;
    virtual std::chrono::steady_clock::time_point now() const noexcept = 0;
};