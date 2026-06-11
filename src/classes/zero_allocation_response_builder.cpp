#include <charconv>
#include <cstring>
#include <string_view>
#include <vector>
#include "video_frame.hpp"
#include "zero_allocation_response_builder.hpp"

char* writeNewLines(char* position) {
    constexpr char newLines[4] = {'\r', '\n', '\r', '\n'};
    char* newPosition = position - 4;
    std::memcpy(newPosition, newLines, 4);
    return newPosition;
}

char* writeLength(char* position, size_t size) {
    char lb[32];
    auto [ptr, ec] = std::to_chars(lb, lb + sizeof(lb), size);
    std::string_view lengthStr(lb, ptr - lb);

    char* newPosition = position - lengthStr.size();
    std::memcpy(newPosition, lengthStr.data(), lengthStr.size());
    return newPosition;
}

char* writePrefix(char* position) {
    constexpr std::string_view prefix = "--mjpegstream\r\nContent-Type: image/jpeg\r\nContent-Length: ";

    char* newPosition = position - prefix.size();
    std::memcpy(newPosition, prefix.data(), prefix.size());
    return newPosition;
}

std::string_view ZeroAllocationResponseBuilder::build(VideoFrame& frame) {
    size_t size = frame.contentSize();

    char* payloadPtr = reinterpret_cast<char*>(frame.storage.data()) + VideoFrame::PADDING_SIZE;
    char* startPtr = payloadPtr;
    char* cursor = startPtr;
    
    cursor = writeNewLines(cursor);
    cursor = writeLength(cursor, size);
    cursor = writePrefix(cursor);

    size_t totalPayloadSize = (startPtr - cursor) + size;

    return {cursor, totalPayloadSize};
}