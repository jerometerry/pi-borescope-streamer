#pragma once

#include <cstdint>
#include <cstdlib>
#include <vector>

namespace MjpegFrameExtractor {
	void extractFrames(const std::vector<uint8_t>& fileData);
}
