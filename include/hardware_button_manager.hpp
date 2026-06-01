#pragma once
#include <chrono>
#include <mutex>
class ServerTime;

/**
 * @brief Maintains an internal state of the hardware button based on the hardware flags embedded in USB packets.
 * 
 */
class HardwareButtonManager {
public:
    explicit HardwareButtonManager(const ServerTime& serverTime);

    void registerHardwarePress();

    bool checkAndResetQuickPressTrigger();

private:
    const ServerTime& serverTime_;
    std::mutex mutex_;
    std::chrono::steady_clock::time_point buttonLastSeen_;
    std::chrono::steady_clock::time_point buttonPressStart_;
    bool buttonIsDepressed_{false};
};
