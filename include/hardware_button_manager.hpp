#pragma once
#include <chrono>
#include <mutex>
class ServerTime;

/**
 * @brief The smart filter that translates raw camera signals into clean button clicks.
 * @details The physical camera handle doesn't actually know what a "click" is. Instead, it constantly 
 * sneaks a hidden status report into the live video stream, rapidly broadcasting "the button is currently 
 * pressed!" over and over as long as your finger is held down. To make matters worse, physical 
 * buttons suffer from electrical "bounce," meaning a single press looks like a stuttering mess of 
 * on and off signals.
 * 
 * HardwareButtonManager generates button down and button up events from the firehose of USB packets that contains the
 * cameras hardware state. 
 */
class HardwareButtonManager {
public:
    /**
     * @brief Set up the button tracker.
     * @param serverTime The central clock used to accurately measure millisecond durations.
     */
    explicit HardwareButtonManager(const ServerTime& serverTime);

    /**
     * @brief Tell the manager that the camera is currently reporting a button press.
     * @details The hardware thread fires this rapidly whenever the camera's hidden data 
     * stream says the button is currently down. This function safely filters out the noisy 
     * "bounces" and records the exact moment a true, solid press begins.
     */
    void registerHardwarePress();

    /**
     * @brief Ask if the user just completed a valid snapshot click.
     * @details This is the only question the rest of the software cares about. It checks 
     * if the button was held down long enough to be a real press (ignoring accidental bumps), 
     * but released quickly enough to be a deliberate "click" (rather than a sustained hold). 
     * * If it was a valid click, this function returns true and immediately "resets" the 
     * trigger so the server doesn't accidentally take multiple pictures from a single press.
     * @return True if a clean, quick click just finished. False otherwise.
     */
    bool checkAndResetQuickPressTrigger();

private:
    /**
     * @brief The central clock used to measure the exact length of a button press.
     */
    const ServerTime& serverTime_;

    /**
     * @brief A safety lock so the hardware and network threads don't trip over each other.
     */
    std::mutex mutex_;

    /**
     * @brief The exact time we last received a "pressed" signal from the camera hardware.
     */
    std::chrono::steady_clock::time_point buttonLastSeen_;

    /**
     * @brief The exact time the current, solid button press began.
     */
    std::chrono::steady_clock::time_point buttonPressStart_;

    /**
     * @brief Tracks whether the software considers the button to be actively held down.
     */
    bool buttonIsDepressed_{false};
};