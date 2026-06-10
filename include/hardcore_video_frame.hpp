#pragma once
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

struct HardcoreVideoFrame {
	std::vector<uint8_t> storage;
	size_t active_size{0};

	void pre_allocate(size_t size) {
		storage.resize(size);
		active_size = 0;
	}

	void clear() noexcept {
		active_size = 0;
	}

	void append_payload(std::span<const uint8_t> src) noexcept {
		if (active_size + src.size() <= storage.size()) {
			std::memcpy(storage.data() + active_size, src.data(), src.size());
			active_size += src.size();
		}
	}

	void trim(size_t startOffset, size_t endOffset) noexcept {
		if (startOffset < endOffset && endOffset <= active_size) {
			size_t newLength = endOffset - startOffset;
			std::memmove(storage.data(), storage.data() + startOffset, newLength);
			active_size = newLength;
		} else {
			active_size = 0;
		}
	}
};
