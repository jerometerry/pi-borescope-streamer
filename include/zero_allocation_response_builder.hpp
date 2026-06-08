#pragma once
#include <string_view>
struct Buffer;

namespace ZeroAllocationResponseBuilder {
	std::string_view build(Buffer* frame);
}