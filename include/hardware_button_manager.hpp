#pragma once
#include <chrono>
#include <mutex>
class ServerTime;

/** @brief Manages the state of the hardware button.
 */
class HardwareButtonManager {
public:
    /** @brief Constructs a HardwareButtonManager instance.
     *  @param serverTime A reference to the server time instance.
     */
    explicit HardwareButtonManager(const ServerTime& serverTime);

    /** @brief Registers a hardware press event.
     */
    void registerHardwarePress();

    /** @brief Checks if a quick press trigger has been activated and resets it.
     *  @return True if a quick press was detected, false otherwise.
     */
    bool checkAndResetQuickPressTrigger();

private:
    /** @brief A reference to the server time instance.
     */
    const ServerTime& serverTime_;

    /** @brief A mutex to protect the button state.
     */
    std::mutex mutex_;

    /** @brief The time when the button was last seen.
     */
    std::chrono::steady_clock::time_point buttonLastSeen_;

    /** @brief The time when the button press started.
     */
    std::chrono::steady_clock::time_point buttonPressStart_;

    /** @brief Indicates if the button is currently depressed.
     */
    bool buttonIsDepressed_{false};
};
