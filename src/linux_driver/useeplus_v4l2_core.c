// SPDX-License-Identifier: MIT OR GPL-2.0-only

#include <linux/init.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/unaligned.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fh.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>
#include "useeplus.h"

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Jerome Terry");
MODULE_DESCRIPTION("V4L2 driver for Useeplus non-UVC borescopes");
MODULE_VERSION("0.1.0");

#define CAP_DRIVER "Useeplus"
#define CAP_CARD "Useeplus non-UVC Borescope"
#define V4L2_INPUT_NAME "Borescope Lens Channel 0"

static const struct usb_device_id up_table[] = {
	{ USB_DEVICE(0x0329, 0x2022) },
	{ USB_DEVICE(0x2ce3, 0x3828) },
	{ }
};
MODULE_DEVICE_TABLE(usb, up_table);

static const u8 iap_auth_handshake[] = { 0xFF, 0x55, 0xFF, 0x55, 0xEE, 0x10 };
static const u8 start_video_command[] = { 0xBB, 0xAA, 0x05, 0x00, 0x00 };

enum up_config {
	HEARTBEAT_SINK_BUFFER_SIZE = 512,
	HEARTBEAT_SINK_ITERATIONS = 30,
	HEARTBEAT_SINK_TIMEOUT_MS = 100,
	DIAG_LOG_ITERATIONS = 300,
	USB_TIMEOUT_MS = 1000,
};

static int up_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers, unsigned int *nplanes,
			  unsigned int sizes[], struct device *alloc_devs[])
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
	struct up_buffer *buf = container_of(v4l2_buf, struct up_buffer, vb2_buffer);
	unsigned long flags;
	spin_lock_irqsave(&drv_data->ready_queue_lock, flags);
	list_add_tail(&buf->list, &drv_data->ready_queue);
	spin_unlock_irqrestore(&drv_data->ready_queue_lock, flags);
}

static void up_kill_urbs(struct up_drv_data *drv_data)
{
	int i;
	clear_bit(STREAM_CLIENT_READY, &drv_data->streaming);
	clear_bit(STREAM_HW_ACTIVE, &drv_data->streaming);
	// Kill ALL URBs first. This guarantees every callback is stopped
	// and no new ones can be submitted.
	for (i = 0; i < BULK_TRANSFER_COUNT; ++i)
		usb_kill_urb(drv_data->urbs[i]);
	// Safe Zone: No callbacks can possibly be running now.
	// It is now 100% safe to free the memory structures.
	for (i = 0; i < BULK_TRANSFER_COUNT; ++i) {
		if (drv_data->urbs[i]) {
			if (drv_data->urb_buffers[i]) {
				usb_free_coherent(drv_data->usb_dev, BULK_TRANSFER_SIZE,
						  drv_data->urb_buffers[i],
						  drv_data->urb_dma_addrs[i]);
				drv_data->urb_buffers[i] = NULL;
			}
			usb_free_urb(drv_data->urbs[i]);
			drv_data->urbs[i] = NULL;
		}
	}
}

static int up_write_msg(struct up_drv_data *data, u8 ep_addr, const u8 *tokens, size_t len)
{
	int retval;
	int actual_length;
	u8 *dma_buffer;
	dma_buffer = kmemdup(tokens, len, GFP_KERNEL);
	if (!dma_buffer)
		return -ENOMEM;
	retval = usb_bulk_msg(data->usb_dev, usb_sndbulkpipe(data->usb_dev, ep_addr), dma_buffer,
			      len, &actual_length, USB_TIMEOUT_MS);
	kfree(dma_buffer);
	return retval;
}

static int up_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct up_drv_data *drv_data = vb2_get_drv_priv(vq);
	struct up_buffer *buf;
	unsigned long flags;
	int urbs_submitted = 0;
	int i, retval;
	if (test_and_set_bit(STREAM_HW_ACTIVE, &drv_data->streaming))
		return 0;
	spin_lock_irqsave(&drv_data->ready_queue_lock, flags);
	drv_data->frame_len = 0;
	drv_data->frame_id = -1;
	drv_data->building_frame = false;
	spin_unlock_irqrestore(&drv_data->ready_queue_lock, flags);
	// Send hardware initialization commands
	retval = up_write_msg(drv_data, drv_data->iap_out_ep, iap_auth_handshake,
			      sizeof(iap_auth_handshake));
	if (retval) {
		dev_err(&drv_data->itf->dev, "up_write_msg init failed: %d\n", retval);
		goto error_start;
	}
	retval = up_write_msg(drv_data, drv_data->video_out_ep, start_video_command,
			      sizeof(start_video_command));
	if (retval) {
		dev_err(&drv_data->itf->dev, "up_write_msg start failed: %d\n", retval);
		goto error_start;
	}
	// Allow URB callback paths to start passing payloads to buffers
	// We do this before submitting URBs so that the read callbacks can
	// start processing data before we finish initializing all URBs
	set_bit(STREAM_CLIENT_READY, &drv_data->streaming);
	// Ensure the bit is visible to all CPU cores before submitting URBs
	// Required after Non-Value-Returning set_bit operation.
	smp_mb__after_atomic();
	// Submit the URBs
	for (urbs_submitted = 0; urbs_submitted < BULK_TRANSFER_COUNT; ++urbs_submitted) {
		retval = usb_submit_urb(drv_data->urbs[urbs_submitted], GFP_KERNEL);
		if (retval) {
			dev_err(&drv_data->itf->dev, "Failed to submit URBs: %d\n", retval);
			goto error_start;
		}
	}
	return 0;
error_start:
	// Clear the client-ready bit immediately to block incoming URB data paths
	clear_bit(STREAM_CLIENT_READY, &drv_data->streaming);
	// Kill any URBs that were successfully submitted before the failure
	for (i = 0; i < urbs_submitted; ++i)
		usb_kill_urb(drv_data->urbs[i]);
	// Drain the queue and return buffers to userspace per V4L2 spec
	spin_lock_irqsave(&drv_data->ready_queue_lock, flags);
	while (!list_empty(&drv_data->ready_queue)) {
		buf = list_first_entry(&drv_data->ready_queue, struct up_buffer, list);
		list_del(&buf->list);
		// Buffers correctly marked as queued for V4L2 cleanup on start error
		vb2_buffer_done(&buf->vb2_buffer.vb2_buf, VB2_BUF_STATE_QUEUED);
	}
	spin_unlock_irqrestore(&drv_data->ready_queue_lock, flags);
	// Clear the HW guard last so a future start_streaming invocation can re-attempt
	clear_bit(STREAM_HW_ACTIVE, &drv_data->streaming);
	return retval;
}

static void up_stop_streaming(struct vb2_queue *vq)
{
	struct up_drv_data *drv_data = vb2_get_drv_priv(vq);
	struct up_buffer *buf;
	unsigned long flags;
	int i;
	// Signal the callback to STOP processing and STOP resubmitting immediately.
	clear_bit(STREAM_CLIENT_READY, &drv_data->streaming);
	// Ensure all CPU cores see the bit change before we start killing URBs.
	// clear_bit doesn't imply a memory barrier, so we explicitly add one.
	smp_mb__after_atomic();
	// usb_kill_urb blocks until any active callback finishes executing.
	// Since STREAM_CLIENT_READY is now 0, the callback will exit without resubmitting.
	for (i = 0; i < BULK_TRANSFER_COUNT; ++i) {
		if (drv_data->urbs[i])
			usb_kill_urb(drv_data->urbs[i]);
	}
	// Reset the hardware active guard state.
	clear_bit(STREAM_HW_ACTIVE, &drv_data->streaming);
	// Safely drain any buffers that were left over in the queue.
	// Because all URBs are definitively dead now, no one else will touch this list.
	spin_lock_irqsave(&drv_data->ready_queue_lock, flags);
	while (!list_empty(&drv_data->ready_queue)) {
		buf = list_first_entry(&drv_data->ready_queue, struct up_buffer, list);
		list_del(&buf->list);
		// Per V4L2 spec, buffers stopped via stop_streaming must be marked as ERROR
		vb2_buffer_done(&buf->vb2_buffer.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&drv_data->ready_queue_lock, flags);
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

static int up_v4l2_open(struct file *file)
{
	return v4l2_fh_open(file);
}

static int up_v4l2_release(struct file *file)
{
	return _vb2_fop_release(file, NULL);
}

static const struct v4l2_file_operations up_v4l2_fops = {
	.owner = THIS_MODULE,
	.open = up_v4l2_open,
	.release = up_v4l2_release,
	.read = vb2_fop_read,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
	.unlocked_ioctl	= video_ioctl2,
};

static int up_vidioc_querycap(struct file *file, void *priv, struct v4l2_capability *cap)
{
	struct up_drv_data *drv_data = video_drvdata(file);
	strscpy(cap->driver, CAP_DRIVER, sizeof(cap->driver));
	strscpy(cap->card, CAP_CARD, sizeof(cap->card));
	usb_make_path(drv_data->usb_dev, cap->bus_info, sizeof(cap->bus_info));
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_DEVICE_CAPS;
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	return 0;
}

static int up_vidioc_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
	struct up_drv_data *drv_data = video_drvdata(file);
	f->fmt.pix.width = drv_data->width;
	f->fmt.pix.height = drv_data->height;
	f->fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
	f->fmt.pix.field = V4L2_FIELD_NONE;
	f->fmt.pix.bytesperline	= 0;
	f->fmt.pix.sizeimage = MAX_FRAME_SIZE;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
	return 0;
}

static int up_vidioc_enum_fmt_vid_cap(struct file *file, void *priv, struct v4l2_fmtdesc *f)
{
	if (f->index > 0)
		return -EINVAL;
	f->pixelformat = V4L2_PIX_FMT_MJPEG;
	return 0;
}

static int up_vidioc_enum_input(struct file *file, void *priv, struct v4l2_input *inp)
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

static int up_vidioc_g_parm(struct file *file, void *priv, struct v4l2_streamparm *sp)
{
	if (sp->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	sp->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	sp->parm.capture.timeperframe.numerator = 1;
	sp->parm.capture.timeperframe.denominator = 30;

	return 0;
}

static int up_vidioc_s_parm(struct file *file, void *priv, struct v4l2_streamparm *sp)
{
	return up_vidioc_g_parm(file, priv, sp);
}

static const struct v4l2_ioctl_ops up_v4l2_ioctl_ops = {
	.vidioc_querycap = up_vidioc_querycap,
	.vidioc_g_fmt_vid_cap = up_vidioc_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap = up_vidioc_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = up_vidioc_fmt_vid_cap,
	.vidioc_enum_fmt_vid_cap = up_vidioc_enum_fmt_vid_cap,
	.vidioc_enum_input = up_vidioc_enum_input,
	.vidioc_g_input = up_vidioc_g_input,
	.vidioc_s_input = up_vidioc_s_input,
	.vidioc_g_parm = up_vidioc_g_parm,
	.vidioc_s_parm = up_vidioc_s_parm,
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
};

static void up_read_bulk_callback(struct urb *urb)
{
	struct up_drv_data *drv_data = urb->context;
	struct up_parse_ctx ctx = { .index = 0 };
	size_t remaining;
	u8 *dcp;
	int retval;
	// Concurrency safety guard
	if (!test_bit(STREAM_CLIENT_READY, &drv_data->streaming))
		return;
	// Handle URB completion status codes
	if (urb->status) {
		switch (urb->status) {
		case -ENOENT:
		case -ECONNRESET:
		case -ESHUTDOWN:
		case -ENODEV:
			dev_dbg(&urb->dev->dev, "URB stopped cleanly: %d\n", urb->status);
			return;
		case -EPROTO:
			drv_data->dbg_usb_errors++;
			goto resubmit;
		case -EILSEQ:
		case -ECOMM:
			dev_dbg(&urb->dev->dev, "Transient CRC/timeout error: %d\n", urb->status);
			goto resubmit;
		case -EPIPE:
			dev_err(&urb->dev->dev, "Endpoint stalled.\n");
			return;
		default:
			dev_err(&urb->dev->dev, "Uncaught URB error: %d\n", urb->status);
			return;
		}
	}
	drv_data->dbg_urbs_processed++;
	// Diagnostic logging throttle
	if (drv_data->dbg_urbs_processed % DIAG_LOG_ITERATIONS == 0) {
		dev_dbg(&drv_data->itf->dev, DIAG_DATA_FORMAT,
			drv_data->dbg_urbs_processed, drv_data->dbg_usb_errors,
			drv_data->dbg_packets_found, drv_data->dbg_frames_found,
			drv_data->dbg_frames_delivered, drv_data->dbg_frames_dropped_soi,
			drv_data->dbg_frames_dropped_eoi, drv_data->dbg_frames_dropped_queue,
			drv_data->dbg_ghost_headers);
	}
	// Append incoming block to decoding workspace
	if (drv_data->decode_buf_len + urb->actual_length <= BULK_TRANSFER_SIZE * 2) {
		memcpy(drv_data->decode_buf + drv_data->decode_buf_len,
		       urb->transfer_buffer, urb->actual_length);
		drv_data->decode_buf_len += urb->actual_length;
	} else {
		dev_warn(&drv_data->itf->dev, "Parse buffer overflow, dropping data\n");
		drv_data->decode_buf_len = 0;
	}
	// Run the decoupled Protocol Decoding Machine
	up_decode_packets(drv_data, &ctx);
	// Shift fractional remaining elements down to the buffer head
	if (ctx.index < drv_data->decode_buf_len) {
		remaining = drv_data->decode_buf_len - ctx.index;
		dcp = drv_data->decode_buf;
		memmove(dcp, dcp + ctx.index, remaining);
		drv_data->decode_buf_len = remaining;
	} else {
		drv_data->decode_buf_len = 0;
	}
resubmit:
	// Safe Pipeline Resubmission check
	if (test_bit(STREAM_CLIENT_READY, &drv_data->streaming)) {
		retval = usb_submit_urb(urb, GFP_ATOMIC);
		if (retval && retval != -ENODEV && retval != -ESHUTDOWN && retval != -ENOENT)
			dev_err(&drv_data->itf->dev, "usb_submit_urb failed: %d\n", retval);
	}
}

static int up_alloc_urbs(struct up_drv_data *drv_data)
{
	struct usb_device *usb_dev = drv_data->usb_dev;
	struct usb_interface *interface = drv_data->itf;
	int i;
	for (i = 0; i < BULK_TRANSFER_COUNT; ++i) {
		drv_data->urbs[i] = usb_alloc_urb(0, GFP_KERNEL);
		if (!drv_data->urbs[i]) {
			dev_err(&interface->dev, "usb_alloc_urb failed\n");
			return -ENOMEM;
		}
		drv_data->urb_buffers[i] = usb_alloc_coherent(usb_dev, BULK_TRANSFER_SIZE,
							      GFP_KERNEL,
							      &drv_data->urb_dma_addrs[i]);
		if (!drv_data->urb_buffers[i]) {
			dev_err(&interface->dev, "usb_alloc_coherent failed\n");
			return -ENOMEM;
		}
		usb_fill_bulk_urb(drv_data->urbs[i], usb_dev,
				  usb_rcvbulkpipe(usb_dev, drv_data->video_in_ep),
				  drv_data->urb_buffers[i], BULK_TRANSFER_SIZE,
				  up_read_bulk_callback, drv_data);
		drv_data->urbs[i]->transfer_dma = drv_data->urb_dma_addrs[i];
		drv_data->urbs[i]->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	}
	return 0;
}

static int up_suspend(struct usb_interface *intf, pm_message_t message)
{
	return 0;
}

static int up_resume(struct usb_interface *intf)
{
	return 0;
}

static void up_disconnect(struct usb_interface *interface)
{
	struct up_drv_data *drv_data = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);
	// Ignore the iAP interface disconnect.
	// The Video Interface disconnect handles the full device teardown.
	if (interface->cur_altsetting->desc.bInterfaceNumber == UP_IAP_INTERFACE)
		return;
	if (drv_data) {
		up_kill_urbs(drv_data);
		// Safely check if V4L2 actually registered before unregistering
		if (video_is_registered(&drv_data->video_dev))
			video_unregister_device(&drv_data->video_dev);
		v4l2_device_disconnect(&drv_data->v4l2_dev);
		v4l2_device_put(&drv_data->v4l2_dev);
		dev_info(&interface->dev, "Useeplus protocol borescope detached.\n");
	}
}

static int up_probe(struct usb_interface *interface, const struct usb_device_id *id);

static struct usb_driver up_driver = {
	.name = "useeplus",
	.id_table = up_table,
	.probe = up_probe,
	.disconnect = up_disconnect,
	.suspend = up_suspend,
	.resume = up_resume,
	.reset_resume = up_resume,
};

static void up_device_release(struct v4l2_device *v4l2_dev)
{
	struct up_drv_data *drv_data = container_of(v4l2_dev, struct up_drv_data, v4l2_dev);
	vfree(drv_data->frame_buf);
	kfree(drv_data->decode_buf);
	kfree(drv_data);
}

static int up_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_device *usb_dev = interface_to_usbdev(interface);
	struct usb_interface *iap_intf;
	struct usb_endpoint_descriptor *ep_desc;
	struct usb_host_interface *video_alt;
	struct up_drv_data *drv_data = NULL;
	struct vb2_queue *q;
	u8 *iap_heartbeat_sink;
	int i, retval, actual_len;
	// Only bind the driver when the Video Interface is probed
	if (interface->cur_altsetting->desc.bInterfaceNumber != UP_VIDEO_INTERFACE)
		return -ENODEV;
	dev_info(&interface->dev, "Useeplus borescope identified\n");
	// Allocate the device state FIRST so we have a valid pointer
	drv_data = kzalloc(sizeof(*drv_data), GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;
	drv_data->usb_dev = usb_dev;
	drv_data->itf = interface;
	drv_data->sequence = 0;
	drv_data->building_frame = false;
	drv_data->frame_len = 0;
	drv_data->decode_buf_len = 0;
	drv_data->width = UP_DEF_WIDTH;
	drv_data->height = UP_DEF_HEIGHT;
	mutex_init(&drv_data->v4l2_lock);
	spin_lock_init(&drv_data->ready_queue_lock);
	INIT_LIST_HEAD(&drv_data->ready_queue);
	// Grab and Claim the iAP Interface
	iap_intf = usb_ifnum_to_if(usb_dev, UP_IAP_INTERFACE);
	if (!iap_intf) {
		dev_err(&interface->dev, "Could not find iAP interface\n");
		retval = -ENODEV;
		goto error_free_dev;
	}
	retval = usb_driver_claim_interface(&up_driver, iap_intf, drv_data);
	if (retval) {
		dev_err(&interface->dev, "Could not claim iAP interface\n");
		goto error_free_dev;
	}
	drv_data->frame_buf = vzalloc(MAX_FRAME_SIZE);
	if (!drv_data->frame_buf) {
		retval = -ENOMEM;
		goto error_release_iap;
	}
	drv_data->decode_buf = kzalloc(BULK_TRANSFER_SIZE * 2, GFP_KERNEL);
	if (!drv_data->decode_buf) {
		retval = -ENOMEM;
		goto error_release_iap;
	}
	// Dynamically Map Endpoints for VIDEO interface (Must look at Altsetting 1)
	video_alt = usb_altnum_to_altsetting(interface, UP_ALT_VIDEO_ENABLE);
	if (!video_alt) {
		dev_err(&interface->dev, "Could not find Video Altsetting\n");
		retval = -ENODEV;
		goto error_release_iap;
	}
	for (i = 0; i < video_alt->desc.bNumEndpoints; ++i) {
		ep_desc = &video_alt->endpoint[i].desc;
		if (usb_endpoint_num(ep_desc) == UP_VIDEO_ENDPOINT) {
			if (usb_endpoint_dir_in(ep_desc))
				drv_data->video_in_ep = ep_desc->bEndpointAddress;
			else
				drv_data->video_out_ep = ep_desc->bEndpointAddress;
		}
	}
	// Dynamically Map Endpoints for iAP interface
	for (i = 0; i < iap_intf->cur_altsetting->desc.bNumEndpoints; ++i) {
		ep_desc = &iap_intf->cur_altsetting->endpoint[i].desc;
		if (usb_endpoint_num(ep_desc) == UP_IAP_ENDPOINT) {
			if (usb_endpoint_dir_in(ep_desc))
				drv_data->iap_in_ep = ep_desc->bEndpointAddress;
			else
				drv_data->iap_out_ep = ep_desc->bEndpointAddress;
		}
	}
	if (!drv_data->video_in_ep || !drv_data->video_out_ep || !drv_data->iap_in_ep ||
	    !drv_data->iap_out_ep) {
		dev_err(&interface->dev, "Could not map all 4 required bulk endpoints\n");
		retval = -ENODEV;
		goto error_release_iap;
	}
	// V4L2 Device Registration
	drv_data->v4l2_dev.release = up_device_release;
	retval = v4l2_device_register(&interface->dev, &drv_data->v4l2_dev);
	if (retval) {
		dev_err(&interface->dev, "v4l2_device_register failed with error %d\n", retval);
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
	q->dev = &interface->dev;
	strscpy(q->name, "useeplus-queue", sizeof(q->name));
	retval = vb2_queue_init(q);
	if (retval) {
		dev_err(&interface->dev, "vb2_queue_init failed\n");
		goto error_unreg_v4l2;
	}
	strscpy(drv_data->video_dev.name, "useeplus-video", sizeof(drv_data->video_dev.name));
	drv_data->video_dev.v4l2_dev = &drv_data->v4l2_dev;
	drv_data->video_dev.fops = &up_v4l2_fops;
	drv_data->video_dev.ioctl_ops = &up_v4l2_ioctl_ops;
	drv_data->video_dev.release = video_device_release_empty;
	drv_data->video_dev.lock = &drv_data->v4l2_lock;
	drv_data->video_dev.queue = q;
	drv_data->video_dev.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	video_set_drvdata(&drv_data->video_dev, drv_data);
	// Hardware Initialization (iAP Drain)
	iap_heartbeat_sink = kmalloc(HEARTBEAT_SINK_BUFFER_SIZE, GFP_KERNEL);
	if (!iap_heartbeat_sink) {
		retval = -ENOMEM;
		goto error_unreg_v4l2;
	}
	for (i = 0; i < HEARTBEAT_SINK_ITERATIONS; ++i) {
		usb_bulk_msg(usb_dev, usb_rcvbulkpipe(usb_dev, drv_data->iap_in_ep),
			     iap_heartbeat_sink, HEARTBEAT_SINK_BUFFER_SIZE, &actual_len,
			     HEARTBEAT_SINK_TIMEOUT_MS);
	}
	kfree(iap_heartbeat_sink);
	retval = usb_set_interface(usb_dev, UP_VIDEO_INTERFACE, UP_ALT_VIDEO_ENABLE);
	if (retval) {
		dev_err(&interface->dev, "usb_set_interface failed with error %d\n", retval);
		goto error_unreg_v4l2;
	}
	retval = usb_clear_halt(usb_dev, usb_rcvbulkpipe(usb_dev, drv_data->video_in_ep));
	if (retval)
		dev_info(&interface->dev, "usb_clear_halt failed with error %d\n", retval);
	retval = up_alloc_urbs(drv_data);
	if (retval)
		goto error_urbs;
	usb_set_intfdata(interface, drv_data);
	retval = video_register_device(&drv_data->video_dev, VFL_TYPE_VIDEO, -1);
	if (retval) {
		dev_err(&interface->dev, "video_register_device failed with error %d\n", retval);
		goto error_urbs;
	}
	dev_info(&interface->dev, "Useeplus protocol borescope connected successfully.\n");
	return 0;
error_urbs:
	dev_dbg(&interface->dev, "Rolling back URBs\n");
	up_kill_urbs(drv_data);
error_unreg_v4l2:
	dev_dbg(&interface->dev, "Unregistering V4L2 device\n");
	v4l2_device_unregister(&drv_data->v4l2_dev);
	return retval;
error_release_iap:
	usb_driver_release_interface(&up_driver, iap_intf);
	vfree(drv_data->frame_buf);
	kfree(drv_data->decode_buf);
error_free_dev:
	kfree(drv_data);
	return retval;
}

static int __init up_init(void)
{
	pr_debug("up_v4l2: Module initialized. Version: %s\n", BUILD_VER);
	return usb_register(&up_driver);
}

static void __exit up_exit(void)
{
	pr_debug("up_v4l2: Module exited.\n");
	usb_deregister(&up_driver);
}

module_init(up_init);
module_exit(up_exit);
