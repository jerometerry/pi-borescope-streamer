/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _up_H_
#define _up_H_

#include "useeplus_protocol.h"
#include <linux/types.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/workqueue.h>
#include <linux/kfifo.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>

/* Global Protocol Constant Macros */
#define NUM_URBS 4
#define URB_SIZE (16 * 1024)
#define MAX_FRAME_SIZE (256 * 1024)
#define UP_MAX_WIRE_LEN 1024
#define MAX_WORKSPACE_SIZE (512 * 1024)
#define FIFO_Q_SIZE (256 * 1024)

#define UP_DEF_WIDTH 640
#define UP_DEF_HEIGHT 480

#define JPEG_SOI_MAX_POS 256
#define MAX_GHOST_HEADER_OFFSET 160

/* Global Diagnostic String Macro */
#define DIAG_DATA_FORMAT \
	"URBs:%lu Err:%lu Pkt:%lu Frm:%lu Deliv:%lu D-SOI:%lu D-EOI:%lu D-Q:%lu Ghost:%lu\n"

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

	struct urb *urbs[NUM_URBS];
	u8 *urb_buffers[NUM_URBS];
	dma_addr_t urb_dma_addrs[NUM_URBS];

	unsigned long streaming;

	struct workqueue_struct *wq;
	struct work_struct work;

	struct up_buffer *active_buf;
	size_t active_pl_len;
	int frame_id;
	bool building_frame;

	DECLARE_KFIFO_PTR(fifo, u8);

	u8 *decode_buf;
	size_t decode_buf_len;

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

#endif /* _up_H_ */
