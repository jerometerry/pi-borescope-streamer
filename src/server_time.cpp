#include <chrono>
#include "clock.hpp"
#include "server_time.hpp"

ServerTime::ServerTime(const Clock& clock, std::chrono::steady_clock::time_point start) 
    : start_(start), clock_(&clock) {}

long long ServerTime::get() const {
    return getElapsedMilliseconds(start_, this->now());
}

long long ServerTime::getElapsedMilliseconds(
        std::chrono::steady_clock::time_point start, 
        std::chrono::steady_clock::time_point end) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

std::chrono::steady_clock::time_point ServerTime::now() const {
    return clock_->now();
}