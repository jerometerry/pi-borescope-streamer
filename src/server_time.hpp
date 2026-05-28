#pragma once

#include <chrono>
#include <csignal>

class ServerTime {
public:
    explicit ServerTime(std::chrono::steady_clock::time_point serverStartTime);

    long long get() const;

private:
    std::chrono::steady_clock::time_point serverStartTime;
};
