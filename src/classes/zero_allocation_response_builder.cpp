#include <charconv>
#include <cstring>
#include <string_view>
#include <vector>
#include "video_frame.hpp"
#include "zero_allocation_response_builder.hpp"

std::string_view ZeroAllocationResponseBuilder::build(VideoFrame& frame) {
    size_t size = frame.contentSize();

    // buffer has 128 bytes reserved for zero-byte allocations
    // startPtr is offset 128 bytes from the start of the allocated buffer

    char* payloadPtr = reinterpret_cast<char*>(frame.storage.data()) + VideoFrame::PADDING_SIZE;
    char* startPtr = payloadPtr;
    char* cursor = startPtr;
    
    constexpr char newLines[4] = {'\r', '\n', '\r', '\n'};

    // Insert 2 line separator
    cursor -= 4;
    std::memcpy(cursor, newLines, 4);
    
    char lb[32];
    auto [ptr, ec] = std::to_chars(lb, lb + sizeof(lb), size);
    std::string_view lengthStr(lb, ptr - lb);
    
    // Write number of bytes in the jpeg
    cursor -= lengthStr.size();
    std::memcpy(cursor, lengthStr.data(), lengthStr.size());
    
    constexpr std::string_view clHeader = "Content-Length: ";

    // Write "Content-Length: " header
    cursor -= clHeader.size();
    std::memcpy(cursor, clHeader.data(), clHeader.size());

    constexpr std::string_view prefix = "--mjpegstream\r\nContent-Type: image/jpeg\r\n";

    // Write "Content-Type: " header
    cursor -= prefix.size();
    std::memcpy(cursor, prefix.data(), prefix.size());

    size_t totalPayloadSize = (startPtr - cursor) + size;

    return {cursor, totalPayloadSize};
}