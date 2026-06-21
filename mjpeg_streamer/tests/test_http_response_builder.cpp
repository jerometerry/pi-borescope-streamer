#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "video_frame_fragment.hpp"
#include "http_response_builder.hpp"

TEST(ZeroAllocationResponseBuilderTest, Build) {
    VideoFrameFragment frame;
    std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};
    frame.insertContent(payload);
    auto response = HttpResponseBuilder::build(frame);
    EXPECT_EQ(
        response,
        "--mjpegstream\r\nContent-Type: image/jpeg\r\nContent-Length: 4\r\n\r\n\xDE\xAD\xBE\xEF");
}
