#include <gtest/gtest.h>
#include <vector>

extern "C" {
    #include "useeplus_protocol.h"
}

struct MockContext {
    int frames_started = 0;
    int frames_ended = 0;
    std::vector<uint8_t> payload_data;
};

static void mock_on_frame_start(void* ctx, u8 frame_id, u8 cam_num) {
    static_cast<MockContext*>(ctx)->frames_started++;
}
static void mock_on_video_payload(void* ctx, u8* data, size_t len) {
    auto* mock = static_cast<MockContext*>(ctx);
    mock->payload_data.insert(mock->payload_data.end(), data, data + len);
}
static void mock_on_frame_end(void* ctx) {
    static_cast<MockContext*>(ctx)->frames_ended++;
}

TEST(ParserTest, SuccessfullyExtractsAndTrimsVideoFrame) {
    MockContext mock_ctx{};
    struct up_parser parser = {0};
    parser.ctx = &mock_ctx;
    parser.cb.on_frame_start = mock_on_frame_start;
    parser.cb.on_video_payload = mock_on_video_payload;
    parser.cb.on_frame_end = mock_on_frame_end;

    std::vector<u8> buffer(1024, 0x00);
    struct up_pkt_hdr* pkt = (struct up_pkt_hdr*)buffer.data();
    pkt->le_delimeter = UP_LE16_TO_CPU(0xBBAA);
    pkt->le_device_id = 0x0B;
    pkt->le_length = UP_LE16_TO_CPU(sizeof(struct up_pl_hdr) + 6);

    struct up_pl_hdr* pl = (struct up_pl_hdr*)(buffer.data() + sizeof(struct up_pkt_hdr));
    pl->le_frame_id = 1;

    u8* payload_data = (u8*)(pl + 1);
    payload_data[0] = 0x99;
    payload_data[1] = 0xFF;
    payload_data[2] = 0xD8;
    payload_data[3] = 0xAA;
    payload_data[4] = 0xFF;
    payload_data[5] = 0xD9;

    size_t consumed = up_parser_feed(&parser, buffer.data(), 1024);

    EXPECT_EQ(mock_ctx.frames_started, 1);
    EXPECT_EQ(mock_ctx.frames_ended, 1);

    EXPECT_EQ(mock_ctx.payload_data.size(), 5);
    EXPECT_EQ(mock_ctx.payload_data[0], 0xFF);
    EXPECT_EQ(mock_ctx.payload_data[1], 0xD8);
}
