#pragma once
#include "clock.hpp"
#include <chrono>

class WallClock : public Clock {
public:
    std::chrono::steady_clock::time_point now() const noexcept override {
        return std::chrono::steady_clock::now();
    }
};