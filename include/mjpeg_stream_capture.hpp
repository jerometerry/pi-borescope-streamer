#pragma once
#include <atomic>
#include "usb_device_info.hpp"

namespace MjpegStreamCapture {
	int capture(const std::atomic<bool>& running, const UsbDeviceInfo& cameraInfo);
	int capture();
}
