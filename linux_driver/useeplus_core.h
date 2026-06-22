/* SPDX-License-Identifier: MIT OR GPL-2.0-only */

#ifndef _USEEPLUS_CORE_H_
#define _USEEPLUS_CORE_H_

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

#define USB_DRIVER_NAME "useeplus"
#define CAP_DRIVER "useeplus"
#define CAP_CARD "useeplus protocol cameras"
#define V4L2_INPUT_NAME "Camera Lens Channel 0"
#define VIDEO_QUEUE_NAME "useeplus-queue"
#define VIDEO_DEVICE_NAME "useeplus-video"

#define NUM_VB2_BUFS 4
#define NUM_URBS 16
#define URB_SIZE (64 * 1024)
#define MAX_FRAME_SIZE (512 * 1024)
#define MAX_WORKSPACE_SIZE (512 * 1024)
#define FIFO_Q_SIZE (2048 * 1024)

#define UP_DEF_WIDTH 640
#define UP_DEF_HEIGHT 480

#define DIAG_DATA_FORMAT \
	"URBs:%lu Err:%lu Pkt:%lu Frm:%lu Deliv:%lu D-SOI:%lu D-EOI:%lu D-Q:%lu Ghost:%lu\n"

enum up_config {
	HB_BUF_SIZE = 512,
	HB_SINK_COUNT = 30,
	HB_SINK_TO = 100,
	DIAG_LOG_ITERATIONS = 300,
	USB_TO = 1000,
	USB_CTRL_TO = 5000,
};

struct up_buffer {
	struct vb2_v4l2_buffer vb2_buffer;
	struct list_head       list;
};

enum up_stream_state {
	STREAM_HW_ACTIVE = 0,
	STREAM_CLIENT_READY = 1,
};

struct up_drv_data {
	struct {
		struct urb	     *urbs[NUM_URBS];
		u8		     *urb_buffers[NUM_URBS];
		struct usb_device    *udev;
		struct usb_interface *itf;
		u8		      video_out_ep;
		u8		      video_in_ep;
		u8		      iap_out_ep;
		u8		      iap_in_ep;
		dma_addr_t	      urb_dma_addrs[NUM_URBS];
	} usb;

	struct {
		struct video_device video_dev;
		struct v4l2_device  v4l2_dev;
		struct v4l2_fract   timeperframe;

		struct vb2_queue queue;
		// Mutex protecting the video_queue
		struct mutex lock;
		u32	     height;
		u32	     width;
		u8           current_hw_index;
	} v4l2;

	struct {
		unsigned long streaming;
		// Spinlock protecting access to ready_queue
		spinlock_t	 ready_lock;
		struct list_head ready_queue;
		u64		 sequence;
	} pipeline;

	struct {
		struct workqueue_struct *wq;
		struct work_struct	 work;
		DECLARE_KFIFO_PTR(fifo, u8);

		u8    *workspace_buf;
		size_t workspace_len;

		struct up_buffer *active_buf;
		size_t		  active_pl_len;

		bool building_frame;
		bool eof_reached;
		bool found_soi;
		int  frame_id;
	} decoder;

	struct {
		unsigned long urbs_processed;
		unsigned long usb_errors;
		unsigned long packets_found;
		unsigned long frames_found;
		unsigned long frames_dropped_soi;
		unsigned long frames_dropped_eoi;
		unsigned long frames_dropped_queue;
		unsigned long frames_delivered;
		unsigned long ghost_headers;
	} dbg;
};

struct uvc_probe_commit_control {
	__u16 bmHint;
	__u8  bFormatIndex;
	__u8  bFrameIndex;
	__le32 dwFrameInterval;
	__u16 wKeyFrameRate;
	__u16 wPFrameRate;
	__u16 wCompQuality;
	__u16 wCompWindowSize;
	__u16 Delay;
	__u32 dwMaxVideoFrameSize;
	__u32 dwMaxPayloadTransferSize;
} __packed;


#endif /* _USEEPLUS_CORE_H_ */
