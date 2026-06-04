#pragma once
#include <atomic>
#include "device_info.hpp"

namespace BinaryStreamCapture {
	int capture(const std::atomic<bool> running, const DeviceInfo& cameraInfo);
	int capture();
}