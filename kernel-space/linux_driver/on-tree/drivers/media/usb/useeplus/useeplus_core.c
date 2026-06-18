// SPDX-License-Identifier: MIT OR GPL-2.0-only

#include "useeplus_core.h"
#include "useeplus_protocol.h"
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/unaligned.h>
#include <linux/usb.h>
#include <linux/vmalloc.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Jerome Terry");
MODULE_DESCRIPTION("V4L2 driver for Useeplus protocol cameras");
MODULE_VERSION("0.1.0");

#define USB_DRIVER_NAME "useeplus"
#define CAP_DRIVER "useeplus"
#define CAP_CARD "useeplus protocol cameras"
#define V4L2_INPUT_NAME "Camera Lens Channel 0"
#define VIDEO_QUEUE_NAME "useeplus-queue"
#define VIDEO_DEVICE_NAME "useeplus-video"

static struct usb_driver up_driver;
static const struct v4l2_ioctl_ops up_v4l2_ioctl_ops;
static const struct v4l2_file_operations up_v4l2_fops;
static const struct vb2_ops up_vb2_ops;

static const u8 iap_auth_handshake[] = { 0xFF, 0x55, 0xFF, 0x55, 0xEE, 0x10 };
static const u8 start_video_command[] = { 0xBB, 0xAA, 0x05, 0x00, 0x00 };

enum up_config {
	HB_BUF_SIZE = 512,
	HB_SINK_COUNT = 30,
	HB_SINK_TO = 100,
	DIAG_LOG_ITERATIONS = 300,
	USB_TO = 1000,
};

static void up_free_urb(struct up_drv_data *drv_data, int urb_index)
{
	struct usb_device *u_dev = drv_data->usb_dev;
	dma_addr_t dma_addr;
	u8 *urb_buf;

	if (!drv_data->urbs[urb_index])
		return;

	urb_buf = drv_data->urb_buffers[urb_index];
	dma_addr = drv_data->urb_dma_addrs[urb_index];
	if (urb_buf) {
		usb_free_coherent(u_dev, URB_SIZE, urb_buf, dma_addr);
		drv_data->urb_buffers[urb_index] = NULL;
	}

	usb_free_urb(drv_data->urbs[urb_index]);
	drv_data->urbs[urb_index] = NULL;
}

static void up_free_urbs(struct up_drv_data *drv_data)
{
	int i;

	clear_bit(STREAM_CLIENT_READY, &drv_data->streaming);
	clear_bit(STREAM_HW_ACTIVE, &drv_data->streaming);

	/*
	 * Ensure every callback is stopped and no new ones can be submitted.
	 */
	for (i = 0; i < NUM_URBS; i++)
		usb_kill_urb(drv_data->urbs[i]);

	/*
	 * Release URB resources
	 */
	for (i = 0; i < NUM_URBS; i++)
		up_free_urb(drv_data, i);
}

static void up_read_bulk_callback(struct urb *urb)
{
	struct up_drv_data *drv_data = urb->context;
	int retval;

	/*
	 * Concurrency safety guard
	 */
	if (!test_bit(STREAM_CLIENT_READY, &drv_data->streaming))
		return;

	/*
	 * Handle URB completion status codes
	 */
	if (urb->status) {
		switch (urb->status) {
		case -ENOENT:
		case -ECONNRESET:
		case -ESHUTDOWN:
		case -ENODEV:
			dev_dbg(&urb->dev->dev, "URB stopped cleanly: %d\n",
				urb->status);
			return;
		case -EPROTO:
			drv_data->dbg_usb_errors++;
			goto resubmit;
		case -EILSEQ:
		case -ECOMM:
			dev_dbg(&urb->dev->dev,
				"Transient CRC/timeout error: %d\n",
				urb->status);
			goto resubmit;
		case -EPIPE:
			dev_err(&urb->dev->dev, "Endpoint stalled.\n");
			return;
		default:
			dev_err(&urb->dev->dev, "Uncaught URB error: %d\n",
				urb->status);
			return;
		}
	}

	drv_data->dbg_urbs_processed++;
	/*
	 * Diagnostic logging throttle
	 */
	if (drv_data->dbg_urbs_processed % DIAG_LOG_ITERATIONS == 0) {
		dev_dbg(&drv_data->itf->dev, DIAG_DATA_FORMAT,
			drv_data->dbg_urbs_processed, drv_data->dbg_usb_errors,
			drv_data->dbg_packets_found, drv_data->dbg_frames_found,
			drv_data->dbg_frames_delivered,
			drv_data->dbg_frames_dropped_soi,
			drv_data->dbg_frames_dropped_eoi,
			drv_data->dbg_frames_dropped_queue,
			drv_data->dbg_ghost_headers);
	}

	if (kfifo_avail(&drv_data->fifo) >= urb->actual_length) {
		kfifo_in(&drv_data->fifo, urb->transfer_buffer,
			 urb->actual_length);
	} else {
		dev_warn(&urb->dev->dev,
			 "kfifo overflow, dropping URB payload\n");
	}

	queue_work(drv_data->wq, &drv_data->work);

resubmit:
	/*
	 * Safe Pipeline Resubmission check
	 */
	if (test_bit(STREAM_CLIENT_READY, &drv_data->streaming)) {
		retval = usb_submit_urb(urb, GFP_ATOMIC);
		if (retval && retval != -ENODEV && retval != -ESHUTDOWN &&
		    retval != -ENOENT)
			dev_err(&drv_data->itf->dev,
				"usb_submit_urb failed: %d\n", retval);
	}
}

static int up_alloc_urbs(struct up_drv_data *drv_data)
{
	struct usb_device *usb_dev = drv_data->usb_dev;
	struct usb_interface *itf = drv_data->itf;
	u8 *urb_ptr;
	int i;

	for (i = 0; i < NUM_URBS; i++) {
		drv_data->urbs[i] = usb_alloc_urb(0, GFP_KERNEL);
		if (!drv_data->urbs[i]) {
			dev_err(&itf->dev, "usb_alloc_urb failed\n");
			return -ENOMEM;
		}

		urb_ptr = usb_alloc_coherent(usb_dev, URB_SIZE, GFP_KERNEL,
					     &drv_data->urb_dma_addrs[i]);
		drv_data->urb_buffers[i] = urb_ptr;

		if (!drv_data->urb_buffers[i]) {
			dev_err(&itf->dev, "usb_alloc_coherent failed\n");
			return -ENOMEM;
		}

		usb_fill_bulk_urb(drv_data->urbs[i], usb_dev,
				  usb_rcvbulkpipe(usb_dev,
						  drv_data->video_in_ep),
				  drv_data->urb_buffers[i], URB_SIZE,
				  up_read_bulk_callback, drv_data);

		drv_data->urbs[i]->transfer_dma = drv_data->urb_dma_addrs[i];
		drv_data->urbs[i]->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	}

	return 0;
}

static void up_device_release(struct v4l2_device *v4l2_dev)
{
	struct up_drv_data *drv_data;

	drv_data = container_of(v4l2_dev, struct up_drv_data, v4l2_dev);

	kfifo_free(&drv_data->fifo);
	kfree(drv_data->decode_buf);
	kfree(drv_data);
}

static void up_on_video_payload(void *context, u8 *data, size_t len)
{
	struct up_drv_data *drv_data = (struct up_drv_data *)context;
	struct vb2_v4l2_buffer *v4l2_buf;
	struct up_buffer *active_buf;
	struct vb2_buffer *vb2_buf;
	struct device *dev;
	size_t pl_len;
	u8 *vaddr;

	if (!drv_data->active_buf)
		return;

	active_buf = drv_data->active_buf;
	v4l2_buf = &active_buf->vb2_buffer;
	vb2_buf = &v4l2_buf->vb2_buf;

	pl_len = drv_data->active_pl_len;
	if (pl_len + len > MAX_FRAME_SIZE) {
		dev = &drv_data->itf->dev;
		dev_err_ratelimited(dev, "useeplus: Overflow Prevention.\n");

		vb2_buffer_done(vb2_buf, VB2_BUF_STATE_ERROR);

		drv_data->active_buf = NULL;
		drv_data->active_pl_len = 0;
		drv_data->dbg_frames_dropped_soi++;

		return;
	}

	vaddr = vb2_plane_vaddr(vb2_buf, 0);

	if (vaddr) {
		memcpy(vaddr + pl_len, data, len);
		drv_data->active_pl_len += len;
	}
}

static void up_on_frame_start(void *context, u8 frame_id, u8 dev_num)
{
	struct up_drv_data *drv_data = (struct up_drv_data *)context;
	struct up_buffer *active_buf;
	struct list_head *rdy_q;
	unsigned long flags;

	if (drv_data->active_buf) {
		active_buf = drv_data->active_buf;
		vb2_buffer_done(&active_buf->vb2_buffer.vb2_buf,
				VB2_BUF_STATE_ERROR);
		drv_data->active_buf = NULL;
	}

	drv_data->active_pl_len = 0;

	rdy_q = &drv_data->ready_queue;

	spin_lock_irqsave(&drv_data->ready_queue_lock, flags);
	if (!list_empty(rdy_q)) {
		active_buf = list_first_entry(rdy_q, struct up_buffer, list);
		list_del(&active_buf->list);

		drv_data->active_buf = active_buf;
	} else {
		drv_data->active_buf = NULL;
	}
	spin_unlock_irqrestore(&drv_data->ready_queue_lock, flags);
}

static void up_on_frame_complete(void *context)
{
	struct up_drv_data *drv_data = (struct up_drv_data *)context;
	struct vb2_v4l2_buffer *v4l2_buf;
	struct up_buffer *active_buf;
	struct vb2_buffer *vb2_buf;
	size_t pl_len;

	if (!drv_data->active_buf)
		return;

	active_buf = drv_data->active_buf;
	v4l2_buf = &active_buf->vb2_buffer;
	vb2_buf = &v4l2_buf->vb2_buf;

	pl_len = drv_data->active_pl_len;
	if (pl_len < 2) {
		drv_data->dbg_frames_dropped_eoi++;
		vb2_buffer_done(vb2_buf, VB2_BUF_STATE_ERROR);
	} else {
		vb2_set_plane_payload(vb2_buf, 0, pl_len);

		vb2_buf->timestamp = ktime_get_ns();
		v4l2_buf->sequence = drv_data->sequence++;

		vb2_buffer_done(vb2_buf, VB2_BUF_STATE_DONE);

		drv_data->dbg_frames_delivered++;
	}

	drv_data->active_buf = NULL;
	drv_data->active_pl_len = 0;
}

static void up_on_frame_incomplete(void *context)
{
	struct up_drv_data *drv_data = (struct up_drv_data *)context;
	struct vb2_v4l2_buffer *v4l2_buf;
	struct up_buffer *active_buf;
	struct vb2_buffer *vb2_buf;

	if (!drv_data->active_buf)
		return;

	active_buf = drv_data->active_buf;
	v4l2_buf = &active_buf->vb2_buffer;
	vb2_buf = &v4l2_buf->vb2_buf;

	/*
	 * The hardware dropped the End of Image (EOI) marker and rolled over.
	 * Return the buffer to the V4L2 subsystem with an error state to
	 * prevent kernel memory starvation and notify userspace of the tear.
	 */
	vb2_buffer_done(vb2_buf, VB2_BUF_STATE_ERROR);

	/*
	 * Clear the active pointer so the upcoming on_frame_start
	 * knows it needs to pull a fresh buffer from the ready_queue.
	 */
	drv_data->active_buf = NULL;
	drv_data->active_pl_len = 0;
}

static void up_work_handler(struct work_struct *work)
{
	size_t consumed, remaining, buf_len;
	struct up_decoder decoder = { 0 };
	struct up_drv_data *drv_data;
	unsigned int len;
	u8 *buf, *dec_buf;

	drv_data = container_of(work, struct up_drv_data, work);

	dec_buf = drv_data->decode_buf;
	buf = dec_buf + drv_data->decode_buf_len;
	buf_len = MAX_WORKSPACE_SIZE - drv_data->decode_buf_len;
	len = kfifo_out(&drv_data->fifo, buf, buf_len);

	drv_data->decode_buf_len += len;

	if (drv_data->decode_buf_len > 0) {
		decoder.context = drv_data;
		decoder.building_frame = drv_data->building_frame;
		decoder.frame_id = drv_data->frame_id;
		decoder.found_soi = drv_data->found_soi;
		decoder.eof_reached = drv_data->eof_reached;

		decoder.cb.on_frame_start = up_on_frame_start;
		decoder.cb.on_video_payload = up_on_video_payload;
		decoder.cb.on_frame_complete = up_on_frame_complete;
		decoder.cb.on_frame_incomplete = up_on_frame_incomplete;

		buf_len = drv_data->decode_buf_len;
		consumed = up_decode_bulk(&decoder, dec_buf, buf_len);

		drv_data->building_frame = decoder.building_frame;
		drv_data->frame_id = decoder.frame_id;
		drv_data->found_soi = decoder.found_soi;
		drv_data->eof_reached = decoder.eof_reached;

		buf_len = drv_data->decode_buf_len;
		if (consumed < buf_len) {
			remaining = buf_len - consumed;
			memmove(dec_buf, dec_buf + consumed, remaining);
			drv_data->decode_buf_len = remaining;
		} else {
			drv_data->decode_buf_len = 0;
		}
	}
}

static int up_probe(struct usb_interface *itf, const struct usb_device_id *id)
{
	int i, retval, hb_bytes, vid_in_pipe, iap_in_pipe, itf_num;
	struct usb_endpoint_descriptor *ep_desc;
	struct usb_host_interface *video_alt;
	struct usb_interface *iap_intf;
	struct up_drv_data *drv_data;
	struct usb_device *usb_dev;
	struct vb2_queue *q;
	u8 *hb_sink, ep;

	usb_dev = interface_to_usbdev(itf);
	itf_num = itf->cur_altsetting->desc.bInterfaceNumber;
	drv_data = NULL;

	/*
	 * Only bind the driver when the Video Interface is probed
	 */
	if (itf_num != UP_VIDEO_INTERFACE)
		return -ENODEV;

	dev_info(&itf->dev, "Useeplus borescope identified\n");

	drv_data = kzalloc(sizeof(*drv_data), GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;

	drv_data->usb_dev = usb_dev;
	drv_data->itf = itf;
	drv_data->sequence = 0;
	drv_data->building_frame = false;
	drv_data->active_pl_len = 0;
	drv_data->decode_buf_len = 0;
	drv_data->width = UP_DEF_WIDTH;
	drv_data->height = UP_DEF_HEIGHT;

	mutex_init(&drv_data->v4l2_lock);
	spin_lock_init(&drv_data->ready_queue_lock);
	INIT_LIST_HEAD(&drv_data->ready_queue);

	iap_intf = usb_ifnum_to_if(usb_dev, UP_IAP_INTERFACE);
	if (!iap_intf) {
		dev_err(&itf->dev, "Could not find iAP interface\n");
		retval = -ENODEV;
		goto error_free_dev;
	}

	retval = usb_driver_claim_interface(&up_driver, iap_intf, drv_data);
	if (retval) {
		dev_err(&itf->dev, "Could not claim iAP interface\n");
		goto error_free_dev;
	}

	drv_data->decode_buf = kzalloc(MAX_WORKSPACE_SIZE, GFP_KERNEL);
	if (!drv_data->decode_buf) {
		retval = -ENOMEM;
		goto error_release_iap;
	}

	INIT_WORK(&drv_data->work, up_work_handler);

	drv_data->wq = alloc_ordered_workqueue("useeplus_wq", WQ_MEM_RECLAIM);
	if (!drv_data->wq) {
		dev_err(&itf->dev, "Could not allocate workqueue\n");
		retval = -ENOMEM;
		goto error_release_iap;
	}

	if (kfifo_alloc(&drv_data->fifo, FIFO_Q_SIZE, GFP_KERNEL)) {
		dev_err(&itf->dev, "Could not allocate FIFO queue\n");
		retval = -ENOMEM;
		goto error_release_iap;
	}

	video_alt = usb_altnum_to_altsetting(itf, UP_ALT_VIDEO_ENABLE);
	if (!video_alt) {
		dev_err(&itf->dev, "Could not find Video Altsetting\n");
		retval = -ENODEV;
		goto error_release_iap;
	}

	for (i = 0; i < video_alt->desc.bNumEndpoints; i++) {
		ep_desc = &video_alt->endpoint[i].desc;
		ep = ep_desc->bEndpointAddress;

		if (usb_endpoint_num(ep_desc) == UP_VIDEO_ENDPOINT) {
			if (usb_endpoint_dir_in(ep_desc))
				drv_data->video_in_ep = ep;
			else
				drv_data->video_out_ep = ep;
		}
	}

	for (i = 0; i < iap_intf->cur_altsetting->desc.bNumEndpoints; i++) {
		ep_desc = &iap_intf->cur_altsetting->endpoint[i].desc;
		ep = ep_desc->bEndpointAddress;

		if (usb_endpoint_num(ep_desc) == UP_IAP_ENDPOINT) {
			if (usb_endpoint_dir_in(ep_desc))
				drv_data->iap_in_ep = ep;
			else
				drv_data->iap_out_ep = ep;
		}
	}

	if (!drv_data->video_in_ep || !drv_data->video_out_ep ||
	    !drv_data->iap_in_ep || !drv_data->iap_out_ep) {
		dev_err(&itf->dev, "Could not map all endpoints\n");
		retval = -ENODEV;
		goto error_release_iap;
	}

	vid_in_pipe = usb_rcvbulkpipe(usb_dev, drv_data->video_in_ep);
	iap_in_pipe = usb_rcvbulkpipe(usb_dev, drv_data->iap_in_ep);

	drv_data->v4l2_dev.release = up_device_release;
	retval = v4l2_device_register(&itf->dev, &drv_data->v4l2_dev);
	if (retval) {
		dev_err(&itf->dev,
			"v4l2_device_register failed with error %d\n", retval);
		goto error_release_iap;
	}

	q = &drv_data->video_queue;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_READ;
	q->drv_priv = drv_data;
	q->buf_struct_size = sizeof(struct up_buffer);
	q->ops = &up_vb2_ops;
	q->mem_ops = &vb2_vmalloc_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->min_queued_buffers = 2;
	q->lock = &drv_data->v4l2_lock;
	q->dev = &itf->dev;
	strscpy(q->name, VIDEO_QUEUE_NAME, sizeof(q->name));

	retval = vb2_queue_init(q);
	if (retval) {
		dev_err(&itf->dev, "vb2_queue_init failed\n");
		goto error_unreg_v4l2;
	}

	strscpy(drv_data->video_dev.name, VIDEO_DEVICE_NAME,
		sizeof(drv_data->video_dev.name));
	drv_data->video_dev.v4l2_dev = &drv_data->v4l2_dev;
	drv_data->video_dev.fops = &up_v4l2_fops;
	drv_data->video_dev.ioctl_ops = &up_v4l2_ioctl_ops;
	drv_data->video_dev.release = video_device_release_empty;
	drv_data->video_dev.lock = &drv_data->v4l2_lock;
	drv_data->video_dev.queue = q;
	drv_data->video_dev.device_caps = V4L2_CAP_VIDEO_CAPTURE |
					  V4L2_CAP_STREAMING;
	video_set_drvdata(&drv_data->video_dev, drv_data);

	hb_sink = kmalloc(HB_BUF_SIZE, GFP_KERNEL);
	if (!hb_sink) {
		retval = -ENOMEM;
		goto error_unreg_v4l2;
	}

	/*
	 * Prune the incoming iAP heartbeats
	 */
	for (i = 0; i < HB_SINK_COUNT; i++) {
		usb_bulk_msg(usb_dev, iap_in_pipe, hb_sink, HB_BUF_SIZE,
			     &hb_bytes, HB_SINK_TO);
	}

	kfree(hb_sink);

	retval = usb_set_interface(usb_dev, UP_VIDEO_INTERFACE,
				   UP_ALT_VIDEO_ENABLE);
	if (retval) {
		dev_err(&itf->dev,
			"usb_set_interface failed with error %d\n", retval);
		goto error_unreg_v4l2;
	}

	retval = usb_clear_halt(usb_dev, vid_in_pipe);
	if (retval)
		dev_info(&itf->dev,
			 "usb_clear_halt failed with error %d\n", retval);

	retval = up_alloc_urbs(drv_data);
	if (retval)
		goto error_urbs;

	usb_set_intfdata(itf, drv_data);

	retval =
		video_register_device(&drv_data->video_dev, VFL_TYPE_VIDEO, -1);
	if (retval) {
		dev_err(&itf->dev,
			"video_register_device failed with error %d\n", retval);
		goto error_urbs;
	}

	dev_info(&itf->dev, "Device connected.\n");
	return 0;

error_urbs:
	dev_dbg(&itf->dev, "Rolling back URBs\n");
	up_free_urbs(drv_data);

error_unreg_v4l2:
	dev_dbg(&itf->dev, "Unregistering device\n");
	v4l2_device_unregister(&drv_data->v4l2_dev);

error_release_iap:
	usb_driver_release_interface(&up_driver, iap_intf);
	kfifo_free(&drv_data->fifo);
	if (drv_data->wq)
		destroy_workqueue(drv_data->wq);
	kfree(drv_data->decode_buf);

error_free_dev:
	kfree(drv_data);
	return retval;
}

static int up_write_msg(struct up_drv_data *data, u8 ep_addr, const u8 *tokens,
			size_t len)
{
	int retval, sent_bytes, out_pipe;
	struct usb_device *u_dev;
	u8 *buf;

	u_dev = data->usb_dev;
	buf = kmemdup(tokens, len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	out_pipe = usb_sndbulkpipe(u_dev, ep_addr);
	retval = usb_bulk_msg(u_dev, out_pipe, buf, len, &sent_bytes, USB_TO);

	kfree(buf);
	return retval;
}

static int up_iap_auth(struct up_drv_data *drv_data)
{
	size_t size = sizeof(iap_auth_handshake);
	int ep = drv_data->iap_out_ep;

	return up_write_msg(drv_data, ep, iap_auth_handshake, size);
}

static int up_start_video(struct up_drv_data *drv_data)
{
	size_t size = sizeof(start_video_command);
	int ep = drv_data->video_out_ep;

	return up_write_msg(drv_data, ep, start_video_command, size);
}

static int up_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	int i, retval, urb_sub;
	struct up_drv_data *drv_data;
	struct usb_interface *itf;
	struct up_buffer *buf;
	unsigned long flags;

	drv_data = vb2_get_drv_priv(vq);
	itf = drv_data->itf;
	if (test_and_set_bit(STREAM_HW_ACTIVE, &drv_data->streaming))
		return 0;

	spin_lock_irqsave(&drv_data->ready_queue_lock, flags);
	drv_data->active_buf = NULL;
	drv_data->active_pl_len = 0;
	drv_data->frame_id = -1;
	drv_data->building_frame = false;
	drv_data->decode_buf_len = 0;
	kfifo_reset(&drv_data->fifo);
	spin_unlock_irqrestore(&drv_data->ready_queue_lock, flags);

	retval = up_iap_auth(drv_data);
	if (retval) {
		dev_err(&itf->dev, "up_iap_auth failed: %d\n", retval);
		goto error_start;
	}

	retval = up_start_video(drv_data);
	if (retval) {
		dev_err(&itf->dev, "up_start_video failed: %d\n", retval);
		goto error_start;
	}

	/*
	 * Allow URB callback paths to start passing payloads to buffers
	 * We do this before submitting URBs so that the read callbacks can
	 * start processing data before we finish initializing all URBs
	 */
	set_bit(STREAM_CLIENT_READY, &drv_data->streaming);

	/*
	 * Ensure the bit is visible to all CPU cores before submitting URBs
	 * Required after Non-Value-Returning set_bit operation.
	 */
	smp_mb__after_atomic();

	/*
	 * Submit the URBs
	 */
	for (urb_sub = 0; urb_sub < NUM_URBS; urb_sub++) {
		retval = usb_submit_urb(drv_data->urbs[urb_sub],
					GFP_KERNEL);
		if (retval) {
			dev_err(&drv_data->itf->dev,
				"Failed to submit URBs: %d\n", retval);
			goto error_start;
		}
	}

	return 0;

error_start:
	/*
	 * Clear the client-ready bit immediately to block incoming URB data paths
	 */
	clear_bit(STREAM_CLIENT_READY, &drv_data->streaming);

	/*
	 * Free any URBs that were successfully submitted before the failure
	 */
	for (i = 0; i < urb_sub; i++)
		usb_kill_urb(drv_data->urbs[i]);

	/*
	 * Drain the queue and return buffers to userspace per V4L2 spec
	 */
	spin_lock_irqsave(&drv_data->ready_queue_lock, flags);
	while (!list_empty(&drv_data->ready_queue)) {
		buf = list_first_entry(&drv_data->ready_queue, struct up_buffer,
				       list);
		list_del(&buf->list);
		/*
		 * Buffers correctly marked as queued for V4L2 cleanup on start error
		 */
		vb2_buffer_done(&buf->vb2_buffer.vb2_buf, VB2_BUF_STATE_QUEUED);
	}
	spin_unlock_irqrestore(&drv_data->ready_queue_lock, flags);

	/*
	 * Clear the HW guard last so a future start_streaming invocation can re-attempt
	 */
	clear_bit(STREAM_HW_ACTIVE, &drv_data->streaming);

	return retval;
}

static void up_stop_streaming(struct vb2_queue *vq)
{
	struct up_drv_data *drv_data;
	struct up_buffer *buf;
	unsigned long flags;
	int i;

	drv_data = vb2_get_drv_priv(vq);
	/*
	 * Signal the callback to STOP processing and STOP resubmitting immediately.
	 */
	clear_bit(STREAM_CLIENT_READY, &drv_data->streaming);

	/*
	 * Ensure all CPU cores see the bit change before we start Freeing URBs.
	 * clear_bit doesn't imply a memory barrier, so we explicitly add one.
	 */
	smp_mb__after_atomic();

	/*
	 * usb_kill_urb blocks until any active callback finishes executing.
	 * Since STREAM_CLIENT_READY is now 0, the callback will exit without
	 * resubmitting.
	 */
	for (i = 0; i < NUM_URBS; i++) {
		if (drv_data->urbs[i])
			usb_kill_urb(drv_data->urbs[i]);
	}

	cancel_work_sync(&drv_data->work);

	if (drv_data->active_buf) {
		vb2_buffer_done(&drv_data->active_buf->vb2_buffer.vb2_buf,
				VB2_BUF_STATE_ERROR);
		drv_data->active_buf = NULL;
		drv_data->active_pl_len = 0;
	}

	/*
	 * Safely drain any buffers that were left over in the queue.
	 * Because all URBs are definitively dead now, no one else will touch
	 * this list.
	 */
	spin_lock_irqsave(&drv_data->ready_queue_lock, flags);
	while (!list_empty(&drv_data->ready_queue)) {
		buf = list_first_entry(&drv_data->ready_queue, struct up_buffer,
				       list);
		list_del(&buf->list);
		/*
		 * Per V4L2 spec, buffers stopped via stop_streaming must be marked as ERROR
		 */
		vb2_buffer_done(&buf->vb2_buffer.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&drv_data->ready_queue_lock, flags);

	/*
	 * Reset the hardware active guard state.
	 */
	clear_bit(STREAM_HW_ACTIVE, &drv_data->streaming);
}

static void up_disconnect(struct usb_interface *itf)
{
	struct usb_interface *iap_intf;
	struct up_drv_data *drv_data;
	int itf_num;

	drv_data = usb_get_intfdata(itf);
	itf_num = itf->cur_altsetting->desc.bInterfaceNumber;
	usb_set_intfdata(itf, NULL);

	/*
	 * Ignore the iAP interface disconnect.
	 * The Video Interface disconnect handles the full device teardown.
	 */
	if (itf_num == UP_IAP_INTERFACE)
		return;

	if (!drv_data)
		return;

	/*
	 * Explicitly release the iAP interface claimed in probe
	 */
	iap_intf = usb_ifnum_to_if(drv_data->usb_dev, UP_IAP_INTERFACE);
	if (iap_intf) {
		usb_set_intfdata(iap_intf, NULL);
		usb_driver_release_interface(&up_driver, iap_intf);
	}

	up_free_urbs(drv_data);

	cancel_work_sync(&drv_data->work);
	if (drv_data->wq) {
		destroy_workqueue(drv_data->wq);
		drv_data->wq = NULL;
	}

	if (video_is_registered(&drv_data->video_dev))
		video_unregister_device(&drv_data->video_dev);

	v4l2_device_disconnect(&drv_data->v4l2_dev);
	v4l2_device_put(&drv_data->v4l2_dev);

	dev_info(&itf->dev, "Device disconnected.\n");
}

static int up_suspend(struct usb_interface *intf, pm_message_t message)
{
	return 0;
}

static int up_resume(struct usb_interface *intf)
{
	return 0;
}

static int up_v4l2_open(struct file *file)
{
	return v4l2_fh_open(file);
}

static int up_v4l2_release(struct file *file)
{
	return _vb2_fop_release(file, NULL);
}

static int up_vidioc_querycap(struct file *file, void *priv,
			      struct v4l2_capability *cap)
{
	struct up_drv_data *drv_data = video_drvdata(file);

	strscpy(cap->driver, CAP_DRIVER, sizeof(cap->driver));
	strscpy(cap->card, CAP_CARD, sizeof(cap->card));
	usb_make_path(drv_data->usb_dev, cap->bus_info, sizeof(cap->bus_info));

	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
			    V4L2_CAP_DEVICE_CAPS;
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;

	return 0;
}

static int up_vidioc_fmt_vid_cap(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	struct up_drv_data *drv_data = video_drvdata(file);

	f->fmt.pix.width = drv_data->width;
	f->fmt.pix.height = drv_data->height;
	f->fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
	f->fmt.pix.field = V4L2_FIELD_NONE;
	f->fmt.pix.bytesperline = 0;
	f->fmt.pix.sizeimage = MAX_FRAME_SIZE;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;

	return 0;
}

static int up_vidioc_enum_fmt_vid_cap(struct file *file, void *priv,
				      struct v4l2_fmtdesc *f)
{
	if (f->index > 0)
		return -EINVAL;

	f->pixelformat = V4L2_PIX_FMT_MJPEG;

	return 0;
}

static int up_vidioc_enum_input(struct file *file, void *priv,
				struct v4l2_input *inp)
{
	if (inp->index > 0)
		return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	strscpy(inp->name, V4L2_INPUT_NAME, sizeof(inp->name));

	return 0;
}

static int up_vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int up_vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	return i == 0 ? 0 : -EINVAL;
}

static int up_vidioc_g_parm(struct file *file, void *priv,
			    struct v4l2_streamparm *sp)
{
	if (sp->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	sp->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	sp->parm.capture.timeperframe.numerator = 1;
	sp->parm.capture.timeperframe.denominator = 30;

	return 0;
}

static int up_vidioc_s_parm(struct file *file, void *priv,
			    struct v4l2_streamparm *sp)
{
	return up_vidioc_g_parm(file, priv, sp);
}

static int up_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
			  unsigned int *nplanes, unsigned int sizes[],
			  struct device *alloc_devs[])
{
	if (*nplanes)
		return sizes[0] < MAX_FRAME_SIZE ? -EINVAL : 0;

	*nplanes = 1;
	sizes[0] = MAX_FRAME_SIZE;
	return 0;
}

static int up_buf_prepare(struct vb2_buffer *vb)
{
	if (vb2_plane_size(vb, 0) < MAX_FRAME_SIZE)
		return -EINVAL;

	vb2_set_plane_payload(vb, 0, MAX_FRAME_SIZE);
	return 0;
}

static void up_buf_queue(struct vb2_buffer *vb)
{
	struct up_drv_data *drv_data = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *v4l2_buf = to_vb2_v4l2_buffer(vb);
	struct up_buffer *buf;
	unsigned long flags;

	buf = container_of(v4l2_buf, struct up_buffer, vb2_buffer);

	spin_lock_irqsave(&drv_data->ready_queue_lock, flags);
	list_add_tail(&buf->list, &drv_data->ready_queue);
	spin_unlock_irqrestore(&drv_data->ready_queue_lock, flags);
}

static int up_vidioc_enum_framesizes(struct file *file, void *priv,
				     struct v4l2_frmsizeenum *fsize)
{
	struct up_drv_data *drv_data = video_drvdata(file);

	if (fsize->index > 0)
		return -EINVAL;

	if (fsize->pixel_format != V4L2_PIX_FMT_MJPEG)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.width = drv_data->width;
	fsize->discrete.height = drv_data->height;

	return 0;
}

static int up_vidioc_enum_frameintervals(struct file *file, void *priv,
					 struct v4l2_frmivalenum *fival)
{
	struct up_drv_data *drv_data = video_drvdata(file);

	if (fival->index > 0)
		return -EINVAL;

	if (fival->pixel_format != V4L2_PIX_FMT_MJPEG)
		return -EINVAL;

	if (fival->width != drv_data->width ||
	    fival->height != drv_data->height)
		return -EINVAL;

	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete.numerator = 1;
	fival->discrete.denominator = 30;

	return 0;
}

static const struct vb2_ops up_vb2_ops = {
	.queue_setup = up_queue_setup,
	.buf_prepare = up_buf_prepare,
	.buf_queue = up_buf_queue,
	.start_streaming = up_start_streaming,
	.stop_streaming = up_stop_streaming,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
};

static const struct v4l2_file_operations up_v4l2_fops = {
	.owner = THIS_MODULE,
	.open = up_v4l2_open,
	.release = up_v4l2_release,
	.read = vb2_fop_read,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
};

static const struct v4l2_ioctl_ops up_v4l2_ioctl_ops = {
	.vidioc_querycap = up_vidioc_querycap,
	.vidioc_g_fmt_vid_cap = up_vidioc_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap = up_vidioc_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = up_vidioc_fmt_vid_cap,
	.vidioc_enum_fmt_vid_cap = up_vidioc_enum_fmt_vid_cap,
	.vidioc_enum_framesizes = up_vidioc_enum_framesizes,
	.vidioc_enum_frameintervals = up_vidioc_enum_frameintervals,
	.vidioc_enum_input = up_vidioc_enum_input,
	.vidioc_g_input = up_vidioc_g_input,
	.vidioc_s_input = up_vidioc_s_input,
	.vidioc_g_parm = up_vidioc_g_parm,
	.vidioc_s_parm = up_vidioc_s_parm,
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
};

static const struct usb_device_id up_table[] = { { USB_DEVICE(0x0329, 0x2022) },
						 { USB_DEVICE(0x2ce3, 0x3828) },
						 {} };

MODULE_DEVICE_TABLE(usb, up_table);

static struct usb_driver up_driver = {
	.name = USB_DRIVER_NAME,
	.id_table = up_table,
	.probe = up_probe,
	.disconnect = up_disconnect,
	.suspend = up_suspend,
	.resume = up_resume,
	.reset_resume = up_resume,
};

static int __init up_init(void)
{
	pr_debug("useeplus_v4l2: Module initialized.\n");
	return usb_register(&up_driver);
}

static void __exit up_exit(void)
{
	pr_debug("useeplus_v4l2: Module exited.\n");
	usb_deregister(&up_driver);
}

module_init(up_init);
module_exit(up_exit);
