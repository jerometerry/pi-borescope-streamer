#pragma once
#include <cstdint>

namespace Arguments {
	enum class ParseResult : std::uint8_t {
		Success,
		HelpRequested,
		Error
	};
}