#pragma once
#include <atomic>

struct UsbDeviceInfo;

namespace MjpegStreamCapture {
	int capture(const std::atomic<bool>& running, const UsbDeviceInfo& cameraInfo);
	int capture();
}