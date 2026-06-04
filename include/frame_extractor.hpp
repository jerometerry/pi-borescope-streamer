#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <format>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "data_structures.hpp"
#include "server_constants.hpp"

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