#pragma once
#include <chrono>
#include <mutex>
#include "server_time.hpp"
#include "server_constants.hpp"
#include "hardware_button_manager.hpp"

HardwareButtonManager::HardwareButtonManager(const ServerTime& serverTime) 
	: serverTime_(serverTime), buttonLastSeen_(serverTime.now()) {}

void HardwareButtonManager::registerHardwarePress() {
	auto currentTime = serverTime_.now();
	std::scoped_lock lock(mutex_);
	auto elapsedMs = serverTime_.getElapsedMilliseconds(buttonLastSeen_, currentTime);

	if (!buttonIsDepressed_ || elapsedMs > ServerConstants::BUTTON_DEBOUNCE_TIME_MS) {
		buttonPressStart_ = currentTime;
		buttonIsDepressed_ = true;
	}
	buttonLastSeen_ = currentTime;
}

bool HardwareButtonManager::checkAndResetQuickPressTrigger() {
	auto currentTime = serverTime_.now();
	std::scoped_lock lock(mutex_);

	if (buttonIsDepressed_) {
		auto elapsedMs = serverTime_.getElapsedMilliseconds(buttonLastSeen_, currentTime);

		if (elapsedMs > ServerConstants::QUICK_PRESS_MIN_MS) {
			auto durationMs = serverTime_.getElapsedMilliseconds(buttonPressStart_, buttonLastSeen_);
			buttonIsDepressed_ = false;

			if (durationMs < ServerConstants::QUICK_PRESS_MAX_MS) {
				return true; // Valid snapshot trigger detected
			}
		}
	}
	return false;
}