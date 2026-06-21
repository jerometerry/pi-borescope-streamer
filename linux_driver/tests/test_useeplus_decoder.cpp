#include <gtest/gtest.h>
#include <vector>

extern "C" {
#include "useeplus_protocol.h"
}

struct MockContext {
	int		     video_frames_started = 0;
	int		     video_frame_fragments = 0;
	int		     video_frames_completed = 0;
	int		     video_frames_incomplete = 0;
	std::vector<uint8_t> payload_data;
	std::set<u8>	     unique_frame_ids;
	std::set<u8>	     unique_dev_nums;
};

class DecoderTest : public ::testing::Test {
    private:
	MockContext		    mock_context_;
	struct up_decoder	    decoder_;
	struct up_decoder_callbacks cb_;

    public:
	DecoderTest()
	{
	}

	static void mock_on_video_frame_start(void *context, u8 frame_id,
					      u8 dev_num)
	{
		MockContext *mockContext = static_cast<MockContext *>(context);
		mockContext->video_frames_started++;
		mockContext->unique_frame_ids.insert(frame_id);
		mockContext->unique_dev_nums.insert(dev_num);
	}

	static void mock_on_video_frame_fragment(void *context, u8 *data,
						 size_t len)
	{
		auto *mock = static_cast<MockContext *>(context);
		mock->payload_data.insert(mock->payload_data.end(), data,
					  data + len);
		static_cast<MockContext *>(context)->video_frame_fragments++;
	}

	static void mock_on_video_frame_complete(void *context)
	{
		static_cast<MockContext *>(context)->video_frames_completed++;
	}

	static void mock_on_video_frame_incomplete(void *context)
	{
		static_cast<MockContext *>(context)->video_frames_incomplete++;
	}

    protected:
	void SetUp() override
	{
		cb_.on_video_frame_start = mock_on_video_frame_start;
		cb_.on_video_frame_fragment = mock_on_video_frame_fragment;
		cb_.on_video_frame_complete = mock_on_video_frame_complete;
		cb_.on_video_frame_incomplete = mock_on_video_frame_incomplete;

		decoder_.cb = cb_;
		decoder_.context = &mock_context_;
		decoder_.frame_id = 0, decoder_.building_frame = false;
		decoder_.found_soi = false;
		decoder_.eof_reached = false;
	}

	MockContext &getContext()
	{
		return mock_context_;
	}

	struct up_decoder *getDecoder()
	{
		return &decoder_;
	}

	int getVideoFramesStarted()
	{
		return mock_context_.video_frames_started;
	}

	int getVideoFrameFragments()
	{
		return mock_context_.video_frame_fragments;
	}

	int getVideoFramesCompleted()
	{
		return mock_context_.video_frames_completed;
	}

	int getVideoFramesIncomplete()
	{
		return mock_context_.video_frames_incomplete;
	}

	int getPayloadSize()
	{
		return mock_context_.payload_data.size();
	}

	uint8_t getPayloadByte(size_t index)
	{
		return mock_context_.payload_data[index];
	}

	bool was_on_video_payload_called() {
		return mock_context_.video_frame_fragments > 0;
	}
};

TEST_F(DecoderTest, SuccessfullyExtractsAndTrimsVideoFrame)
{
	std::vector<u8>	       buffer(1024, 0x00);
	struct up_usb_frm_hdr *u_hdr = up_get_usb_frm_hdr(buffer.data(), 0);

	u_hdr->le_delimiter = le16_to_cpu(UP_PKT_DEL);
	u_hdr->device_id = VIDEO_CAMERA_ID;
	u_hdr->le_length = le16_to_cpu(UP_VIDEO_FRM_FRAG_HDR_LEN + 6);

	struct up_video_frm_frag_hdr *v_hdr =
		up_get_video_frm_frag_hdr(buffer.data(), UP_USB_FRM_HDR_LEN);
	v_hdr->frame_id = 1;

	u8 *payload_data = (u8 *)(v_hdr + 1);
	payload_data[0] = 0x99;
	payload_data[1] = JPEG_DEL;
	payload_data[2] = JPEG_SOI;
	payload_data[3] = 0xAA;
	payload_data[4] = JPEG_DEL;
	payload_data[5] = JPEG_EOI;

	size_t consumed = up_decode_bulk(getDecoder(), buffer.data(), 1024);

	EXPECT_EQ(consumed, static_cast<unsigned int>(1013));
	EXPECT_EQ(getVideoFramesStarted(), 1);
	EXPECT_EQ(getVideoFramesCompleted(), 1);
	EXPECT_EQ(getPayloadSize(), 5);
	EXPECT_EQ(getPayloadByte(0), JPEG_DEL);
	EXPECT_EQ(getPayloadByte(1), JPEG_SOI);
}

TEST_F(DecoderTest, SkipsGhostHeadersAndFindsValidPayload)
{
	std::vector<u8> buffer(1024, 0x00);

	for (size_t i = 0; i < 10; i++)
		buffer[i] = 0xAA;

	size_t valid_start = 10;

	struct up_usb_frm_hdr *u_hdr =
		up_get_usb_frm_hdr(buffer.data(), valid_start);

	u_hdr->le_delimiter = le16_to_cpu(UP_PKT_DEL);
	u_hdr->device_id = VIDEO_CAMERA_ID;
	u_hdr->le_length = le16_to_cpu(UP_VIDEO_FRM_FRAG_HDR_LEN + 4);

	struct up_video_frm_frag_hdr *v_hdr = up_get_video_frm_frag_hdr(
		buffer.data(), valid_start + UP_USB_FRM_HDR_LEN);
	v_hdr->frame_id = 2;

	u8 *payload_data = (u8 *)(v_hdr + 1);
	payload_data[0] = JPEG_DEL;
	payload_data[1] = JPEG_SOI;
	payload_data[2] = JPEG_DEL;
	payload_data[3] = JPEG_EOI;

	size_t consumed = up_decode_bulk(getDecoder(), buffer.data(), 1024);
	const size_t expected_consumed = static_cast<size_t>(1013);
	EXPECT_EQ(consumed, expected_consumed);
	EXPECT_EQ(getVideoFramesStarted(), 1);
	EXPECT_EQ(getVideoFramesCompleted(), 1);
	EXPECT_EQ(getPayloadSize(), 4);
}

TEST_F(DecoderTest, ReturnsNeedDataForFragmentedUrbs)
{
	std::vector<u8>	       buffer(1024, 0x00);
	struct up_usb_frm_hdr *u_hdr = up_get_usb_frm_hdr(buffer.data(), 0);

	u_hdr->le_delimiter = le16_to_cpu(UP_PKT_DEL);
	u_hdr->device_id = VIDEO_CAMERA_ID;
	u_hdr->le_length = le16_to_cpu(100);

	size_t consumed = up_decode_bulk(getDecoder(), buffer.data(), 50);

	EXPECT_EQ(consumed, static_cast<unsigned int>(0));
	EXPECT_EQ(getVideoFramesStarted(), 0);
}

std::vector<u8> create_valid_frame(u8 frame_id, size_t *video_data_size) {
	std::vector<u8> buffer(1024, 0x00);

	size_t payload_size = 512;
	*video_data_size = payload_size;

	struct up_usb_frm_hdr *u_hdr = up_get_usb_frm_hdr(buffer.data(), 0);
	u_hdr->le_delimiter = le16_to_cpu(UP_PKT_DEL);
	u_hdr->device_id = VIDEO_CAMERA_ID;
	u_hdr->le_length = le16_to_cpu(UP_VIDEO_FRM_FRAG_HDR_LEN + payload_size);

	struct up_video_frm_frag_hdr *v_hdr =
		up_get_video_frm_frag_hdr(buffer.data(), UP_USB_FRM_HDR_LEN);
	v_hdr->frame_id = frame_id;
	v_hdr->device_number = 1;
	v_hdr->flags = 0;
	v_hdr->le_gravity_sensor = 1;

	u8 *video_data = (u8 *)(buffer.data() + VIDEO_DATA_OFFSET);
	video_data[0] = JPEG_DEL;
	video_data[1] = JPEG_SOI;
	video_data[2] = 0xAA;

	u8 *eoi_ptr = video_data + payload_size - 2;

	eoi_ptr[0] = JPEG_DEL;
	eoi_ptr[1] = JPEG_EOI;

	return buffer;
}

std::vector<u8> create_junk_frame(u8 frame_id) {
	std::vector<u8>	       buffer(1024, 0x00);
	struct up_usb_frm_hdr *u_hdr = up_get_usb_frm_hdr(buffer.data(), 0);

	u_hdr->le_delimiter = le16_to_cpu(UP_PKT_DEL);
	u_hdr->device_id = static_cast<uint8_t>(9);
	u_hdr->le_length = le16_to_cpu(UP_VIDEO_FRM_FRAG_HDR_LEN + 4);

	struct up_video_frm_frag_hdr *v_hdr =
		up_get_video_frm_frag_hdr(buffer.data(), UP_USB_FRM_HDR_LEN);

	v_hdr->frame_id = frame_id;
	up_set_has_gravity_sensor(v_hdr, true);

	u8 *payload_data = (u8 *)(v_hdr + 1);
	payload_data[0] = JPEG_DEL;
	payload_data[2] = JPEG_DEL;
	payload_data[1] = JPEG_SOI;
	payload_data[3] = JPEG_EOI;
	return buffer;
}

TEST_F(DecoderTest, DecoderRecoversAfterInvalidFrame)
{
    std::vector<u8> buffer_a = create_junk_frame(1);

    size_t video_data_size = 0;
    std::vector<u8> buffer_b = create_valid_frame(2, &video_data_size);

    up_decode_bulk(getDecoder(), buffer_a.data(), buffer_a.size());
    EXPECT_EQ(getPayloadSize(), 0);

    up_decode_bulk(getDecoder(), buffer_b.data(), buffer_b.size());

    EXPECT_EQ(getPayloadSize(), video_data_size);
    EXPECT_TRUE(was_on_video_payload_called());
}

TEST_F(DecoderTest, DropsChunkIfSoiNotFound)
{
	std::vector<u8>	       buffer(1024, 0x00);
	struct up_usb_frm_hdr *u_hdr = up_get_usb_frm_hdr(buffer.data(), 0);

	u_hdr->le_delimiter = le16_to_cpu(UP_PKT_DEL);
	u_hdr->device_id = VIDEO_CAMERA_ID;
	u_hdr->le_length = le16_to_cpu(UP_VIDEO_FRM_FRAG_HDR_LEN + 4);

	struct up_video_frm_frag_hdr *v_hdr =
		up_get_video_frm_frag_hdr(buffer.data(), UP_USB_FRM_HDR_LEN);

	v_hdr->frame_id = 1;

	u8 *payload_data = (u8 *)(v_hdr + 1);
	payload_data[0] = 0xAA;
	payload_data[1] = 0xBB;
	payload_data[2] = 0xCC;
	payload_data[3] = 0xDD;

	size_t consumed = up_decode_bulk(getDecoder(), buffer.data(),
					 VIDEO_DATA_OFFSET + 4);

	EXPECT_EQ(consumed, static_cast<unsigned int>(16));
	EXPECT_EQ(consumed, static_cast<unsigned int>(VIDEO_DATA_OFFSET + 4));
	EXPECT_EQ(getVideoFramesStarted(), 1);
	EXPECT_EQ(getPayloadSize(), 0);
}

TEST_F(DecoderTest, HuntsForSignatureOnInvalidPacket)
{
	std::vector<u8> buffer(1024, 0x00);

	buffer[0] = 0xAA;
	buffer[1] = 0xBB;
	buffer[2] = 0xCC;

	size_t		       valid_start = 3;
	struct up_usb_frm_hdr *u_hdr =
		up_get_usb_frm_hdr(buffer.data(), valid_start);

	u_hdr->le_delimiter = le16_to_cpu(UP_PKT_DEL);
	u_hdr->device_id = VIDEO_CAMERA_ID;
	u_hdr->le_length = le16_to_cpu(UP_VIDEO_FRM_FRAG_HDR_LEN + 4);

	struct up_video_frm_frag_hdr *v_hdr = up_get_video_frm_frag_hdr(
		buffer.data(), valid_start + UP_USB_FRM_HDR_LEN);

	v_hdr->frame_id = 1;

	u8 *payload_data = (u8 *)(v_hdr + 1);
	payload_data[0] = JPEG_DEL;
	payload_data[1] = JPEG_SOI;
	payload_data[2] = JPEG_DEL;
	payload_data[3] = JPEG_EOI;

	size_t consumed = up_decode_bulk(getDecoder(), buffer.data(), 1024);

	EXPECT_EQ(consumed, static_cast<unsigned int>(1013));
	EXPECT_EQ(getVideoFramesStarted(), 1);
	EXPECT_EQ(getVideoFramesCompleted(), 1);
	EXPECT_EQ(getPayloadSize(), 4);
}

TEST_F(DecoderTest, RejectsMassiveLengthAndHunts)
{
	std::vector<u8> buffer(1024, 0x00);

	struct up_usb_frm_hdr *bad_pkt = up_get_usb_frm_hdr(buffer.data(), 0);

	bad_pkt->le_delimiter = le16_to_cpu(UP_PKT_DEL);
	bad_pkt->device_id = VIDEO_CAMERA_ID;
	bad_pkt->le_length = le16_to_cpu(UP_MAX_VIDEO_FRM_FRAG_LEN + 100);

	size_t		       valid_start = 200;
	struct up_usb_frm_hdr *good_pkt =
		up_get_usb_frm_hdr(buffer.data(), valid_start);

	good_pkt->le_delimiter = le16_to_cpu(UP_PKT_DEL);
	good_pkt->device_id = VIDEO_CAMERA_ID;
	good_pkt->le_length = le16_to_cpu(UP_VIDEO_FRM_FRAG_HDR_LEN + 4);

	struct up_video_frm_frag_hdr *v_hdr = up_get_video_frm_frag_hdr(
		buffer.data(), valid_start + UP_USB_FRM_HDR_LEN);

	v_hdr->frame_id = 1;

	u8 *payload_data = (u8 *)(v_hdr + 1);

	payload_data[0] = JPEG_DEL;
	payload_data[1] = JPEG_SOI;
	payload_data[2] = JPEG_DEL;
	payload_data[3] = JPEG_EOI;

	size_t consumed = up_decode_bulk(getDecoder(), buffer.data(), 1024);

	EXPECT_EQ(consumed, static_cast<unsigned int>(1013));
	EXPECT_EQ(getVideoFramesStarted(), 1);
	EXPECT_EQ(getVideoFramesCompleted(), 1);
	EXPECT_EQ(getPayloadSize(), 4);
}
