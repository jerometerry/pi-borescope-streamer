/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _up_H_
#define _up_H_

#include <linux/types.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>

/* Global Protocol Constant Macros */
#define BULK_TRANSFER_COUNT		4
#define BULK_TRANSFER_SIZE		(16 * 1024)
#define MAX_FRAME_SIZE			(512 * 1024)

#define UP_DEF_WIDTH	640
#define UP_DEF_HEIGHT	480

#define JPEG_SOI_MAX_POS	256
#define MAX_GHOST_HEADER_OFFSET		160

/* Structural Protocol Sizing Expressions */
#define UP_PKT_HDR_SIZE		(sizeof(struct up_pkt_hdr))
#define UP_PL_HDR_SIZE		(sizeof(struct up_pl_hdr))
#define TOTAL_USB_HEADER_SIZE	(UP_PKT_HDR_SIZE + UP_PL_HDR_SIZE)

/* Global Diagnostic String Macro */
#define DIAG_DATA_FORMAT "URBs:%lu Err:%lu Pkt:%lu Frm:%lu Deliv:%lu D-SOI:%lu D-EOI:%lu D-Q:%lu Ghost:%lu\n"

enum up_usb_topology {
	UP_IAP_INTERFACE	= 0,
	UP_VIDEO_INTERFACE	= 1,
	UP_ALT_VIDEO_ENABLE	= 1,
	UP_VIDEO_ENDPOINT	= 0x01,
	UP_IAP_ENDPOINT		= 0x02,
};

enum up_hw_signatures {
	UP_PKT_DEL	= 0xBBAA,
	VIDEO_CAMERA_ID		= 0x0B,
	GRAVITY_SENSOR_ID	= 0x07,
};

enum up_jpeg_marker {
	JPEG_DEL	= 0xFF,
	JPEG_SOI	= 0xD8,
	JPEG_EOI	= 0xD9,
};

/**
 * struct up_pkt_hdr - High-level wire transfer framing format
 */
struct up_pkt_hdr {
	__le16 le_delimeter;
	u8 le_device_id;
	__le16 le_length;
} __packed;

/**
 * struct up_pl_hdr - Content transport payload structure tracking
 */
struct up_pl_hdr {
	u8 le_frame_id;
	u8 le_camera_number;
	u8 le_flags;
	__le32 le_gravity_sensor;
} __packed;

/**
 * struct up_buffer - Queue wrapper mapping videobuf2 elements to internal lists
 */
struct up_buffer {
	struct vb2_v4l2_buffer vb2_buffer;
	struct list_head list;
};

/**
 * enum up_stream_state - Multi-threaded operational pipeline bit identifiers
 */
enum up_stream_state {
	STREAM_HW_ACTIVE = 0,
	STREAM_CLIENT_READY = 1,
};

/**
 * struct up_drv_data - Main centralized hardware controller structure
 */
struct up_drv_data {
	struct usb_interface *itf;
	u8 video_in_ep;
	u8 video_out_ep;
	u8 iap_in_ep;
	u8 iap_out_ep;

	// Mutex protecting the video_queue
	struct mutex v4l2_lock;
	struct vb2_queue video_queue;

	struct usb_device *usb_dev;
	struct v4l2_device v4l2_dev;
	struct video_device video_dev;
	u32 width;
	u32 height;

	// Spinlock protecting access to ready_queue
	spinlock_t ready_queue_lock;
	struct list_head ready_queue;

	u64 sequence;

	struct urb *urbs[BULK_TRANSFER_COUNT];
	u8 *urb_buffers[BULK_TRANSFER_COUNT];
	dma_addr_t urb_dma_addrs[BULK_TRANSFER_COUNT];

	unsigned long streaming;

	u8 *frame_buf;
	size_t frame_len;
	int frame_id;
	bool building_frame;

	u8 *decode_buf;
	size_t decode_buf_len;

	unsigned int frame_counter;

	unsigned long dbg_urbs_processed;
	unsigned long dbg_ghost_headers;
	unsigned long dbg_packets_found;
	unsigned long dbg_frames_found;
	unsigned long dbg_frames_dropped_soi;
	unsigned long dbg_frames_dropped_eoi;
	unsigned long dbg_frames_dropped_queue;
	unsigned long dbg_frames_delivered;
	unsigned long dbg_usb_errors;
};

/* Unified Interface Function Declaration */
struct up_parse_ctx {
	size_t index;
	unsigned long flags;
};

void up_decode_packets(struct up_drv_data *drv_data,
		       struct up_parse_ctx *ctx);

#endif /* _up_H_ */
