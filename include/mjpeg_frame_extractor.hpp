#pragma once

#include <cstdint>
#include <cstdlib>
#include <vector>

/**
 * @brief Methods for extracting frames from a data stream
 */
namespace MjpegFrameExtractor {
	void extractFrames(const std::vector<uint8_t>& data);
}
