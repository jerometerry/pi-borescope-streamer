#include <gtest/gtest.h>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include "buffer.hpp"
#include "buffer_pool.hpp"
#include "buffer_ptr.hpp"
#include "constants.hpp"
#include "disruptor.hpp"
#include "hardcore_video_frame.hpp"
#include "intrusive_ptr.hpp"
#include "zero_allocation_response_builder.hpp"

TEST(ZeroAllocationResponseBuilderTest, Build) {
    HardcoreVideoFrame frame;
    std::vector<uint8_t> payload = { 0xDE, 0xAD, 0xBE, 0xEF };
    frame.insertContent(payload);
    auto response = ZeroAllocationResponseBuilder::build(frame);
    EXPECT_EQ(response, 
        "--mjpegstream\r\nContent-Type: image/jpeg\r\nContent-Length: 4\r\n\r\n\xDE\xAD\xBE\xEF"
    );
}
