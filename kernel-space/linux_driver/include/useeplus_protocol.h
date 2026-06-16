/* SPDX-License-Identifier: GPL-2.0+ OR MIT */
#ifndef _USEEPLUS_PROTOCOL_H_
#define _USEEPLUS_PROTOCOL_H_

#ifdef __KERNEL__
	#include <linux/types.h>
	#include <asm/byteorder.h>

	#define UP_LE16_TO_CPU(x) le16_to_cpu(x)
	#define UP_LE32_TO_CPU(x) le32_to_cpu(x)
#else
	#include <stdint.h>
	#include <stdbool.h>
	#include <stddef.h>

	typedef uint8_t  u8;
	typedef uint16_t u16;
	typedef uint32_t u32;

	#ifndef __packed
	#define __packed __attribute__((packed))
	#endif

	#if defined(__APPLE__)
		#include <libkern/OSByteOrder.h>
		#define UP_LE16_TO_CPU(x) OSSwapLittleToHostInt16(x)
		#define UP_LE32_TO_CPU(x) OSSwapLittleToHostInt32(x)
	#else
		#include <endian.h>
		#define UP_LE16_TO_CPU(x) le16toh(x)
		#define UP_LE32_TO_CPU(x) le32toh(x)
	#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum up_usb_topology {
	UP_IAP_INTERFACE = 0,
	UP_VIDEO_INTERFACE = 1,
	UP_ALT_VIDEO_ENABLE = 1,
	UP_VIDEO_ENDPOINT = 0x01,
	UP_IAP_ENDPOINT = 0x02,
};

enum up_hw_signatures {
	UP_PKT_DEL = 0xBBAA,
	VIDEO_CAMERA_ID = 0x0B,
	GRAVITY_SENSOR_ID = 0x07,
	MAX_CAM_NUM = 1,
};

enum up_jpeg_marker {
	JPEG_DEL = 0xFF,
	JPEG_SOI = 0xD8,
	JPEG_EOI = 0xD9,
};

struct up_pkt_hdr {
	u16 le_delimeter;
	u8  le_device_id;
	u16 le_length;
} __packed;

struct up_pl_hdr {
	u8  le_frame_id;
	u8  le_camera_number;
	u8  le_flags;
	u32 le_gravity_sensor;
} __packed;

struct up_parser_callbacks {
	void (*on_frame_start)(void *ctx, u8 frame_id, u8 cam_num);
	void (*on_video_payload)(void *ctx, const u8 *data, size_t len);
	void (*on_frame_end)(void *ctx);
};

struct up_parser {
	struct up_parser_callbacks cb;
	void *ctx;
	u8 current_frame_id;
	bool is_building_frame;
};

void up_parser_feed(struct up_parser *parser, const u8 *buffer, size_t len);

static inline bool up_check_pkt_header(u16 delimeter, u8 device_id)
{
	return (delimeter == UP_PKT_DEL &&
		(device_id == VIDEO_CAMERA_ID || device_id == GRAVITY_SENSOR_ID));
}

static inline bool up_is_valid_pkt_header(const struct up_pkt_hdr *pkt)
{
	u16 del = UP_LE16_TO_CPU(pkt->le_delimeter);
	u8 dev_id = pkt->le_device_id;

	return up_check_pkt_header(del, dev_id);
}

#ifdef __cplusplus
}
#endif

#endif /* _USEEPLUS_PROTOCOL_H_ */
