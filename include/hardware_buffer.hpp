#pragma once
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

struct HardwareBuffer {
	std::vector<uint8_t> storage;
	size_t active_size{0};

	void preAllocate(size_t size) {
		storage.resize(size);
		active_size = 0;
	}

	void clear() noexcept {
		active_size = 0;
	}

	void write_payload(std::span<const uint8_t> src) noexcept {
		if (src.size() <= storage.size()) {
			std::memcpy(storage.data(), src.data(), src.size());
			active_size = src.size();
		}
	}
};
