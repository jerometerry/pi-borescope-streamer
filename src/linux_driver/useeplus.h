/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _USEEPLUS_H_
#define _USEEPLUS_H_

#include <linux/types.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>

/* Global Protocol Constant Macros */
#define BULK_TRANSFER_COUNT            4
#define BULK_TRANSFER_SIZE             (16 * 1024)
#define MAX_FRAME_SIZE                 (512 * 1024)

#define USB_PACKET_DELIMITER           0xBBAA
#define VIDEO_CAMERA_ID                1
#define GRAVITY_SENSOR_ID              2

#define JPEG_BOUNDARY_MARKER           0xFF
#define JPEG_START_OF_IMG_MARKER       0xD8
#define JPEG_END_OF_IMG_MARKER         0xD9
#define JPEG_SOI_MARKERS_MAX_POSITION  256
#define MAX_GHOST_HEADER_OFFSET        32

/* Structural Protocol Sizing Expressions */
#define USB_PACKET_HEADER_SIZE         (sizeof(struct usb_packet_header))
#define USB_PAYLOAD_HEADER_SIZE        (sizeof(struct usb_payload_header))
#define TOTAL_USB_HEADER_SIZE          (USB_PACKET_HEADER_SIZE + USB_PAYLOAD_HEADER_SIZE)

/* Global Diagnostic String Macro */
#define DIAG_DATA_FORMAT "URBs:%lu Err:%lu Pkt:%lu Frm:%lu Deliv:%lu D-SOI:%lu D-EOI:%lu D-Q:%lu Ghost:%lu\n"

/**
 * struct usb_packet_header - High-level wire transfer framing format
 */
struct usb_packet_header {
	__le16 le_delimeter;
	u8 le_device_id;
	__le16 le_length;
} __packed;

/**
 * struct usb_payload_header - Content transport payload structure tracking
 */
struct usb_payload_header {
	u8 le_frame_id;
	u8 le_camera_number;
	u8 le_flags;
	__le32 le_gravity_sensor;
} __packed;

/**
 * struct useeplus_buffer - Queue wrapper mapping videobuf2 elements to internal lists
 */
struct useeplus_buffer {
	struct vb2_v4l2_buffer vb2_buffer;
	struct list_head list;
};

/**
 * enum useeplus_stream_state - Multi-threaded operational pipeline bit identifiers
 */
enum useeplus_stream_state {
	STREAM_HW_ACTIVE = 0,
	STREAM_CLIENT_READY = 1,
};

/**
 * struct useeplus_drv_data - Main centralized hardware controller structure
 */
struct useeplus_drv_data {
	struct usb_interface *interface;
	u8 video_in_ep;
	u8 video_out_ep;
	u8 iap_in_ep;
	u8 iap_out_ep;

	struct mutex v4l2_lock;

	struct usb_device *usb_dev;
	struct v4l2_device v4l2_dev;
	struct video_device video_dev;

	struct vb2_queue video_queue;
	struct list_head ready_queue;
	spinlock_t ready_queue_lock;
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
struct useeplus_parse_ctx {
	size_t index;
	unsigned long flags;
};

void useeplus_decode_packets(struct useeplus_drv_data *drv_data,
			     struct useeplus_parse_ctx *ctx);

#endif /* _USEEPLUS_H_ */
