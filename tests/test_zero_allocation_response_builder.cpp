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

    FrameDisruptor ringBuffer;
    for (int64_t i = 0; i < FRAME_DISRUPTOR_CAPACITY; i++) {
        ringBuffer.get_by_sequence(i).pre_allocate(Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);
    }
 
    int64_t next_read = 0;
    int64_t available = ringBuffer.wait_for(next_read);

    while (next_read <= available) {
        HardcoreVideoFrame& slot = ringBuffer.get_by_sequence(next_read);

        if (slot.active_size > 0) {
            std::vector<uint8_t> payload = { 0xDE, 0xAD, 0xBE, 0xEF };
            slot.append_payload(payload);

            auto response = ZeroAllocationResponseBuilder::build(slot);

            EXPECT_EQ(response, 
                "--mjpegstream\r\nContent-Type: image/jpeg\r\nContent-Length: 4\r\n\r\n\xDE\xAD\xBE\xEF"
            );
        }

        next_read++;
    }
    ringBuffer.mark_consumed(next_read - 1);

    
}
