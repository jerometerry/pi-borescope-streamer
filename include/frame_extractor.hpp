#pragma once

#include <cstdint>
#include <cstdlib>
#include <vector>

namespace FrameExtractor {
	struct DumpRange {
		size_t start;
		size_t end;
	};

	struct Padding {
		size_t start;
		size_t length;
	};

	void printPaddingDump(const std::vector<uint8_t>& data, Padding padding);

	void inspectPadding(const std::vector<uint8_t>& fileData);

	void printHexDump(const std::vector<uint8_t>& data, DumpRange range);

	void inspectFrameBoundary(const std::vector<uint8_t>& fileData);

	void extractFrames(const std::vector<uint8_t>& fileData);
}