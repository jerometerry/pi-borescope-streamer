#include "zero_allocation_response_builder.hpp"

#include <charconv>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "video_frame.hpp"

std::string_view ZeroAllocationResponseBuilder::build(VideoFrame& frame) {
    size_t size = frame.contentSize();

    char lb[32];
    auto [ptr, ec] = std::to_chars(lb, lb + sizeof(lb), size);
    size_t lengthStringSize = ptr - lb;

    constexpr std::string_view prefix =
        "--mjpegstream\r\nContent-Type: image/jpeg\r\nContent-Length: ";
    constexpr size_t newLinesSize = 4;

    size_t totalHeaderSize = prefix.size() + lengthStringSize + newLinesSize;

    if (totalHeaderSize > VideoFrame::PADDING_SIZE) {
        throw std::length_error("HTTP Header exceeded reserved padding space");
    }

    char* start = reinterpret_cast<char*>(frame.storage.data());
    char* payloadPtr = start + VideoFrame::PADDING_SIZE;
    char* cursor = payloadPtr;

    cursor -= newLinesSize;
    std::memcpy(cursor, "\r\n\r\n", newLinesSize);

    cursor -= lengthStringSize;
    std::memcpy(cursor, lb, lengthStringSize);

    cursor -= prefix.size();
    std::memcpy(cursor, prefix.data(), prefix.size());

    size_t totalPayloadSize = (payloadPtr - cursor) + size;

    return {cursor, totalPayloadSize};
}
