#pragma once

#include <chrono>
#include "clock.hpp"

class WallClock : public Clock {
public:
    std::chrono::steady_clock::time_point now() const noexcept override {
        return std::chrono::steady_clock::now();
    }
};