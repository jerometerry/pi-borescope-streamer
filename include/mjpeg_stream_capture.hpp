#pragma once
#include <atomic>
struct UsbDeviceInfo;

namespace BinaryStreamCapture {
	int capture(const std::atomic<bool>& running, const UsbkDeviceInfo& cameraInfo);
	int capture();
}
