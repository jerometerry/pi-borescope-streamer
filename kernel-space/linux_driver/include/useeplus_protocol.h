/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _USEEPLUS_PROTOCOL_H_
#define _USEEPLUS_PROTOCOL_H_

#ifdef __KERNEL__
	/* Kernel-space includes and types */
	#include <linux/types.h>
	#include <asm/byteorder.h>

	#define UP_LE16_TO_CPU(x) le16_to_cpu(x)
	#define UP_LE32_TO_CPU(x) le32_to_cpu(x)
#else
	/* User-space includes and types */
	#include <stdint.h>
	#include <stdbool.h>
	#include <stddef.h>

	/* Map kernel shorthand types to standard C99 types */
	typedef uint8_t  u8;
	typedef uint16_t u16;
	typedef uint32_t u32;

	/* Endianness mapping for macOS vs Linux user-space */
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
	uint16_t le_delimeter;
	uint8_t  le_device_id;
	uint16_t le_length;
} __attribute__((packed));

struct up_pl_hdr {
	uint8_t  le_frame_id;
	uint8_t  le_camera_number;
	uint8_t  le_flags;
	uint32_t le_gravity_sensor;
} __attribute__((packed));

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

/* * The Callback Interface
 * The parser will fire these events when it finds valid data.
 */
struct up_parser_callbacks {
	/* Called when a new frame ID is detected */
	void (*on_frame_start)(void *ctx, uint8_t frame_id, uint8_t cam_num);

	/* Called to deliver a chunk of MJPEG payload */
	void (*on_video_payload)(void *ctx, const uint8_t *data, size_t len);

	/* Called when the parser detects the end of a frame (or frame ID change) */
	void (*on_frame_end)(void *ctx);

	/* Optional: Called when parser detects a hardware button press */
	void (*on_button_press)(void *ctx);
};

struct up_parser {
	struct up_parser_callbacks cb;
	void *ctx;

	/* Parser state variables */
	uint8_t current_frame_id;
	bool is_building_frame;
	/* ... */
};

/* The single entry point for feeding raw USB bytes */
void up_parser_feed(struct up_parser *parser, const uint8_t *buffer, size_t len);

#endif