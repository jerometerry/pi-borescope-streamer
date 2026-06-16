#include <gtest/gtest.h>

extern "C" {
    #include "useeplus.h"
}

TEST(ParserTest, SuccessfullyExtractsVideoFrame) {
    struct up_drv_data drv_data = {};
    struct up_parse_ctx ctx = {};

    // 1. Setup the incoming USB ring buffer
    drv_data.decode_buf = new u8[1024];
    drv_data.decode_buf_len = 1024;

    // 2. Setup the V4L2 destination queue
    INIT_LIST_HEAD(&drv_data.ready_queue);

    // 3. Create a mock V4L2 buffer for the parser to write into
    struct up_buffer mock_vid_buf = {};
    uint8_t mock_v4l2_destination[MAX_FRAME_SIZE] = {0};
    mock_vid_buf.vb2_buffer.vb2_buf.mock_vaddr = mock_v4l2_destination;

    // Put our mock buffer in the queue
    list_add_tail(&mock_vid_buf.list, &drv_data.ready_queue);

    // 4. Construct a valid Useeplus packet header
    struct up_pkt_hdr* pkt = (struct up_pkt_hdr*)drv_data.decode_buf;
    pkt->le_delimeter = htole16(0xBBAA);
    pkt->le_device_id = 0x0B; // Valid Video Camera ID
    pkt->le_length = htole16(sizeof(struct up_pl_hdr) + 4); // Header + 4 byte payload

    // 5. Construct a valid Payload header
    struct up_pl_hdr* payload = (struct up_pl_hdr*)(drv_data.decode_buf + sizeof(struct up_pkt_hdr));
    payload->le_frame_id = 1;
    payload->le_camera_number = 1;
    payload->le_flags = 0x00; // No gravity sensor, no invalid flags

    // 6. Add some mock JPEG data
    u8* payload_data = (u8*)(payload + 1);
    payload_data[0] = 0xFF;
    payload_data[1] = 0xD8; // SOI (Start of Image)
    payload_data[2] = 0xAA;
    payload_data[3] = 0xBB;

    // 7. Run the kernel parser!
    up_decode_packets(&drv_data, &ctx);

    // 8. Assert the parser did its job
    EXPECT_EQ(drv_data.dbg_packets_found, 1);

    // Assert the data was successfully copied out of the USB buffer and into our mock V4L2 memory
    EXPECT_EQ(mock_v4l2_destination[0], 0xFF);
    EXPECT_EQ(mock_v4l2_destination[1], 0xD8);
    EXPECT_EQ(mock_v4l2_destination[2], 0xAA);
    EXPECT_EQ(mock_v4l2_destination[3], 0xBB);

    delete[] drv_data.decode_buf;
}