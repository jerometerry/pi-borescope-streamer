#pragma once
#include <atomic>
struct DeviceInfo;

namespace BinaryStreamCapture {
	int capture(const std::atomic<bool>& running, const DeviceInfo& cameraInfo);
	int capture();
}
