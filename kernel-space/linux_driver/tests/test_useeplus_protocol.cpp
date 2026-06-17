#include <gtest/gtest.h>

extern "C" {
#include "useeplus_protocol.h"
}

TEST(ProtocolTest, ValidMjpegPayloadWithDefaultInitializer)
{
	struct up_pl_hdr payload_header = {};
	bool valid;

	valid = up_valid_mjpeg_pl(&payload_header);

	EXPECT_TRUE(valid);
}

TEST(ProtocolTest, ValidMjpegPayloadWithInvalidCameraNumber)
{
	struct up_pl_hdr payload_header = {};
	bool valid;

	payload_header.le_camera_number = 99;
	valid = up_valid_mjpeg_pl(&payload_header);

	EXPECT_FALSE(valid);
}

TEST(ProtocolTest, ValidMjpegPayloadWithHasGravitySensorSet)
{
	struct up_pl_hdr payload_header = {};
	bool valid;

	up_set_has_gravity_sensor(&payload_header, true);
	valid = up_valid_mjpeg_pl(&payload_header);

	EXPECT_FALSE(valid);
}

TEST(ProtocolTest, ValidMjpegPayloadWithButtonPressedSet)
{
	struct up_pl_hdr payload_header = {};
	bool valid;

	up_set_button_pressed(&payload_header, true);
	valid = up_valid_mjpeg_pl(&payload_header);

	EXPECT_TRUE(valid);
}

TEST(ProtocolTest, ValidMjpegPayloadWithOtherFlagsSet)
{
	struct up_pl_hdr payload_header = {};
	bool valid;

	up_set_other_flags(&payload_header, 3);
	valid = up_valid_mjpeg_pl(&payload_header);

	EXPECT_FALSE(valid);
}
