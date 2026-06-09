#pragma once
#include <string_view>

class Buffer;

namespace ZeroAllocationResponseBuilder {
	std::string_view build(Buffer* frame);
}
