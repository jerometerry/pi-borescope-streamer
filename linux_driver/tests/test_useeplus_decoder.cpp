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

static void mock_on_frame_start(void *context, u8 frame_id, u8 dev_num)
{
	std::cout << "frame_id: " << frame_id << " dev_num: " << dev_num
		  << "\n";
	static_cast<MockContext *>(context)->frames_started++;
}
static void mock_on_video_payload(void *context, u8 *data, size_t len)
{
	auto *mock = static_cast<MockContext *>(context);
	mock->payload_data.insert(mock->payload_data.end(), data, data + len);
}
static void mock_on_frame_end(void *context)
{
	static_cast<MockContext *>(context)->frames_ended++;
}

TEST(DecoderTest, SuccessfullyExtractsAndTrimsVideoFrame)
{
	MockContext mock_context{};

	struct up_decoder_callbacks cb = {
		.on_video_frame_start = mock_on_frame_start,
		.on_video_frame_fragment = mock_on_video_payload,
		.on_video_frame_complete = mock_on_frame_end
	};
	struct up_decoder decoder = { .cb = cb,
				      .context = &mock_context,
				      .frame_id = 0,
				      .building_frame = false,
				      .found_soi = false,
				      .eof_reached = false };

	std::vector<u8> buffer(1024, 0x00);
	struct up_usb_frm_hdr *pkt = up_get_usb_frm_hdr(buffer.data(), 0);
	pkt->le_delimiter = UP_LE16_TO_CPU(UP_PKT_DEL);
	pkt->device_id = VIDEO_CAMERA_ID;
	pkt->le_length = UP_LE16_TO_CPU(UP_VIDEO_FRM_FRAG_HDR_SIZE + 6);

	struct up_video_frm_frag_hdr *pl = up_get_video_frm_frag_hdr(buffer.data(), UP_USB_FRM_HDR_SIZE);
	pl->frame_id = 1;

	u8 *payload_data = (u8 *)(pl + 1);
	payload_data[0] = 0x99;
	payload_data[1] = JPEG_DEL;
	payload_data[2] = JPEG_SOI;
	payload_data[3] = 0xAA;
	payload_data[4] = JPEG_DEL;
	payload_data[5] = JPEG_EOI;

	size_t consumed = up_decode_bulk(&decoder, buffer.data(), 1024);

	EXPECT_EQ(consumed, 1013);
	EXPECT_EQ(mock_context.frames_started, 1);
	EXPECT_EQ(mock_context.frames_ended, 1);
	EXPECT_EQ(mock_context.payload_data.size(), 5);
	EXPECT_EQ(mock_context.payload_data[0], JPEG_DEL);
	EXPECT_EQ(mock_context.payload_data[1], JPEG_SOI);
}

TEST(DecoderTest, SkipsGhostHeadersAndFindsValidPayload)
{
	MockContext mock_context{};
	struct up_decoder_callbacks cb = {
		.on_video_frame_start = mock_on_frame_start,
		.on_video_frame_fragment = mock_on_video_payload,
		.on_video_frame_complete = mock_on_frame_end
	};
	struct up_decoder decoder = { .cb = cb,
				      .context = &mock_context,
				      .frame_id = 0,
				      .building_frame = false,
				      .found_soi = false,
				      .eof_reached = false };

	std::vector<u8> buffer(1024, 0x00);

	for (int i = 0; i < 10; i++)
		buffer[i] = 0xAA;

	size_t valid_start = 10;

	struct up_usb_frm_hdr *pkt = up_get_usb_frm_hdr(buffer.data(), valid_start);
	pkt->le_delimiter = UP_LE16_TO_CPU(UP_PKT_DEL);
	pkt->device_id = VIDEO_CAMERA_ID;
	pkt->le_length = UP_LE16_TO_CPU(UP_VIDEO_FRM_FRAG_HDR_SIZE + 4);

	struct up_video_frm_frag_hdr *pl =
		up_get_video_frm_frag_hdr(buffer.data(), valid_start + UP_USB_FRM_HDR_SIZE);
	pl->frame_id = 2;

	u8 *payload_data = (u8 *)(pl + 1);
	payload_data[0] = JPEG_DEL;
	payload_data[1] = JPEG_SOI;
	payload_data[2] = JPEG_DEL;
	payload_data[3] = JPEG_EOI;

	size_t consumed = up_decode_bulk(&decoder, buffer.data(), 1024);

	EXPECT_EQ(consumed, 1013);
	EXPECT_EQ(mock_context.frames_started, 1);
	EXPECT_EQ(mock_context.frames_ended, 1);
	EXPECT_EQ(mock_context.payload_data.size(), 4);
}

TEST(DecoderTest, ReturnsNeedDataForFragmentedUrbs)
{
	MockContext mock_context{};
	struct up_decoder_callbacks cb = {
		.on_video_frame_start = mock_on_frame_start,
		.on_video_frame_fragment = mock_on_video_payload,
		.on_video_frame_complete = mock_on_frame_end
	};
	struct up_decoder decoder = { .cb = cb,
				      .context = &mock_context,
				      .frame_id = 0,
				      .building_frame = false,
				      .found_soi = false,
				      .eof_reached = false };

	std::vector<u8> buffer(1024, 0x00);
	struct up_usb_frm_hdr *pkt = up_get_usb_frm_hdr(buffer.data(), 0);
	pkt->le_delimiter = UP_LE16_TO_CPU(UP_PKT_DEL);
	pkt->device_id = VIDEO_CAMERA_ID;
	pkt->le_length = UP_LE16_TO_CPU(100);

	size_t consumed = up_decode_bulk(&decoder, buffer.data(), 50);

	EXPECT_EQ(consumed, 0);
	EXPECT_EQ(mock_context.frames_started, 0);
}

TEST(DecoderTest, IgnoresInvalidCameraOrTelemetryFrames)
{
	MockContext mock_context{};
	struct up_decoder_callbacks cb = {
		.on_video_frame_start = mock_on_frame_start,
		.on_video_frame_fragment = mock_on_video_payload,
		.on_video_frame_complete = mock_on_frame_end
	};
	struct up_decoder decoder = { .cb = cb,
				      .context = &mock_context,
				      .frame_id = 0,
				      .building_frame = false,
				      .found_soi = false,
				      .eof_reached = false };

	std::vector<u8> buffer(1024, 0x00);
	struct up_usb_frm_hdr *pkt = up_get_usb_frm_hdr(buffer.data(), 0);
	pkt->le_delimiter = UP_LE16_TO_CPU(UP_PKT_DEL);
	pkt->device_id = VIDEO_CAMERA_ID;
	pkt->le_length = UP_LE16_TO_CPU(UP_VIDEO_FRM_FRAG_HDR_SIZE + 4);

	struct up_video_frm_frag_hdr *pl = up_get_video_frm_frag_hdr(buffer.data(), UP_USB_FRM_HDR_SIZE);
	pl->frame_id = 1;
	up_set_has_gravity_sensor(pl, true);

	u8 *payload_data = (u8 *)(pl + 1);
	payload_data[0] = JPEG_DEL;
	payload_data[1] = JPEG_SOI;
	payload_data[2] = JPEG_DEL;
	payload_data[3] = JPEG_EOI;

	size_t consumed =
		up_decode_bulk(&decoder, buffer.data(), VIDEO_DATA_OFFSET + 4);

	EXPECT_EQ(consumed, 16);
	EXPECT_EQ(consumed, VIDEO_DATA_OFFSET + 4);
	EXPECT_EQ(mock_context.payload_data.size(), 0);
}

TEST(DecoderTest, DropsChunkIfSoiNotFound)
{
	MockContext mock_context{};
	struct up_decoder_callbacks cb = {
		.on_video_frame_start = mock_on_frame_start,
		.on_video_frame_fragment = mock_on_video_payload,
		.on_video_frame_complete = mock_on_frame_end
	};
	struct up_decoder decoder = { .cb = cb,
				      .context = &mock_context,
				      .frame_id = 0,
				      .building_frame = false,
				      .found_soi = false,
				      .eof_reached = false };

	std::vector<u8> buffer(1024, 0x00);
	struct up_usb_frm_hdr *pkt = up_get_usb_frm_hdr(buffer.data(), 0);
	pkt->le_delimiter = UP_LE16_TO_CPU(UP_PKT_DEL);
	pkt->device_id = VIDEO_CAMERA_ID;
	pkt->le_length = UP_LE16_TO_CPU(UP_VIDEO_FRM_FRAG_HDR_SIZE + 4);

	struct up_video_frm_frag_hdr *pl = up_get_video_frm_frag_hdr(buffer.data(), UP_USB_FRM_HDR_SIZE);
	pl->frame_id = 1;

	u8 *payload_data = (u8 *)(pl + 1);
	payload_data[0] = 0xAA;
	payload_data[1] = 0xBB;
	payload_data[2] = 0xCC;
	payload_data[3] = 0xDD;

	size_t consumed =
		up_decode_bulk(&decoder, buffer.data(), VIDEO_DATA_OFFSET + 4);

	EXPECT_EQ(consumed, 16);
	EXPECT_EQ(consumed, VIDEO_DATA_OFFSET + 4);
	EXPECT_EQ(mock_context.frames_started, 1);
	EXPECT_EQ(mock_context.payload_data.size(), 0);
}

TEST(DecoderTest, HuntsForSignatureOnInvalidPacket)
{
	MockContext mock_context{};
	struct up_decoder_callbacks cb = {
		.on_video_frame_start = mock_on_frame_start,
		.on_video_frame_fragment = mock_on_video_payload,
		.on_video_frame_complete = mock_on_frame_end
	};
	struct up_decoder decoder = { .cb = cb,
				      .context = &mock_context,
				      .frame_id = 0,
				      .building_frame = false,
				      .found_soi = false,
				      .eof_reached = false };

	std::vector<u8> buffer(1024, 0x00);

	buffer[0] = 0xAA;
	buffer[1] = 0xBB;
	buffer[2] = 0xCC;

	size_t valid_start = 3;
	struct up_usb_frm_hdr *pkt = up_get_usb_frm_hdr(buffer.data(), valid_start);
	pkt->le_delimiter = UP_LE16_TO_CPU(UP_PKT_DEL);
	pkt->device_id = VIDEO_CAMERA_ID;
	pkt->le_length = UP_LE16_TO_CPU(UP_VIDEO_FRM_FRAG_HDR_SIZE + 4);

	struct up_video_frm_frag_hdr *pl =
		up_get_video_frm_frag_hdr(buffer.data(), valid_start + UP_USB_FRM_HDR_SIZE);
	pl->frame_id = 1;

	u8 *payload_data = (u8 *)(pl + 1);
	payload_data[0] = JPEG_DEL;
	payload_data[1] = JPEG_SOI;
	payload_data[2] = JPEG_DEL;
	payload_data[3] = JPEG_EOI;

	size_t consumed = up_decode_bulk(&decoder, buffer.data(), 1024);

	EXPECT_EQ(consumed, 1013);
	EXPECT_EQ(mock_context.frames_started, 1);
	EXPECT_EQ(mock_context.frames_ended, 1);
	EXPECT_EQ(mock_context.payload_data.size(), 4);
}

TEST(DecoderTest, RejectsMassiveLengthAndHunts)
{
	MockContext mock_context{};
	struct up_decoder_callbacks cb = {
		.on_video_frame_start = mock_on_frame_start,
		.on_video_frame_fragment = mock_on_video_payload,
		.on_video_frame_complete = mock_on_frame_end
	};
	struct up_decoder decoder = { .cb = cb,
				      .context = &mock_context,
				      .frame_id = 0,
				      .building_frame = false,
				      .found_soi = false,
				      .eof_reached = false };

	std::vector<u8> buffer(1024, 0x00);

	struct up_usb_frm_hdr *bad_pkt = up_get_usb_frm_hdr(buffer.data(), 0);
	bad_pkt->le_delimiter = UP_LE16_TO_CPU(UP_PKT_DEL);
	bad_pkt->device_id = VIDEO_CAMERA_ID;
	bad_pkt->le_length = UP_LE16_TO_CPU(UP_MAX_VIDEO_FRM_FRAG_LEN + 100);

	size_t valid_start = 200;
	struct up_usb_frm_hdr *good_pkt =
		up_get_usb_frm_hdr(buffer.data(), valid_start);
	good_pkt->le_delimiter = UP_LE16_TO_CPU(UP_PKT_DEL);
	good_pkt->device_id = VIDEO_CAMERA_ID;
	good_pkt->le_length = UP_LE16_TO_CPU(UP_VIDEO_FRM_FRAG_HDR_SIZE + 4);

	struct up_video_frm_frag_hdr *pl =
		up_get_video_frm_frag_hdr(buffer.data(), valid_start + UP_USB_FRM_HDR_SIZE);
	pl->frame_id = 1;

	u8 *payload_data = (u8 *)(pl + 1);
	payload_data[0] = JPEG_DEL;
	payload_data[1] = JPEG_SOI;
	payload_data[2] = JPEG_DEL;
	payload_data[3] = JPEG_EOI;

	size_t consumed = up_decode_bulk(&decoder, buffer.data(), 1024);

	EXPECT_EQ(consumed, 1013);
	EXPECT_EQ(mock_context.frames_started, 1);
	EXPECT_EQ(mock_context.frames_ended, 1);
	EXPECT_EQ(mock_context.payload_data.size(), 4);
}