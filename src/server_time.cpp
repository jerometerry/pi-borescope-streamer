#include "server_time.hpp"

#include <chrono>
#include <csignal>

ServerTime::ServerTime(std::chrono::steady_clock::time_point serverStartTime) : serverStartTime(serverStartTime) {}

long long ServerTime::get() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - serverStartTime).count();
}
