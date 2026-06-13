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

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Jerome Terry");
MODULE_DESCRIPTION("V4L2 driver for Useeplus non-UVC borescopes");
MODULE_VERSION("0.1.0");

#define USB_TIMEOUT_MS			 1000
#define BULK_TRANSFER_COUNT		4

#define BULK_TRANSFER_SIZE		 16384
#define MAX_FRAME_SIZE			 (256 * 1024)

#define NATIVE_PACKET_HEADER_SIZE  5
#define NATIVE_PAYLOAD_HEADER_SIZE 7
#define TOTAL_PROTOCOL_HEADER_SIZE (NATIVE_PACKET_HEADER_SIZE + NATIVE_PAYLOAD_HEADER_SIZE)
#define MAX_SCAN_LIMIT			 160

#define PROTO_FRAME_HEADER_MAGIC   0xBBAA
#define PROTO_FRAME_HEADER_A	   0xAA
#define PROTO_FRAME_HEADER_B	   0xBB
#define PROTO_VIDEO_CAMERA_ID	  0x0B
#define PROTO_GRAVITY_CAMERA_ID	0x07

#define FLAG_STREAMING 0

static const u8 initialization_tokens[]  = { 0xFF, 0x55, 0xFF, 0x55, 0xEE, 0x10 };
static const u8 start_stream_tokens[]	= { 0xBB, 0xAA, 0x05, 0x00, 0x00 };

static const struct usb_device_id useeplus_table[] = {
	{ USB_DEVICE(0x0329, 0x2022) },
	{ USB_DEVICE(0x2ce3, 0x3828) },
	{ }
};
MODULE_DEVICE_TABLE(usb, useeplus_table);

struct usb_packet_header {
	__le16 leHeader;
	u8 leCameraId;
	__le16 leLength;
} __packed;

struct usb_payload_header {
	u8 leFrameId;
	u8 leCameraNumber;
	u8 leFlags;
	__le32 leGravitySensor;
} __packed;

struct useeplus_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

struct usb_useeplus {
	struct usb_device *udev;
	struct usb_interface *interface;

	struct v4l2_device v4l2_dev;
	struct video_device vdev;
	struct mutex v4l2_lock;

	struct vb2_queue vb_vidq;
	struct list_head rdy_queue;
	spinlock_t q_lock;
	u64 sequence;

	struct urb *urbs[BULK_TRANSFER_COUNT];
	u8 *urb_buffers[BULK_TRANSFER_COUNT];
	dma_addr_t urb_dma_addrs[BULK_TRANSFER_COUNT];

	unsigned long flags;
	bool vb_streaming;

	u8 *current_frame;
	size_t current_frame_len;
	int last_frame_id;
	bool has_stored_header;
	struct usb_payload_header active_payload_hdr;

	u8 *parse_buffer;
	size_t parse_len;

	unsigned int frame_counter;

	unsigned long dbg_urbs_processed;
	unsigned long dbg_ghost_headers;
	unsigned long dbg_packets_found;
	unsigned long dbg_frames_found;
	unsigned long dbg_frames_dropped_soi;
	unsigned long dbg_frames_dropped_eoi;
	unsigned long dbg_frames_dropped_queue;
	unsigned long dbg_frames_delivered;
};

static int useeplus_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
				unsigned int *nplanes, unsigned int sizes[],
				struct device *alloc_devs[])
{
	if (*nplanes)
		return sizes[0] < MAX_FRAME_SIZE ? -EINVAL : 0;

	*nplanes = 1;
	sizes[0] = MAX_FRAME_SIZE;
	return 0;
}

static int useeplus_buf_prepare(struct vb2_buffer *vb)
{
	if (vb2_plane_size(vb, 0) < MAX_FRAME_SIZE)
		return -EINVAL;

	vb2_set_plane_payload(vb, 0, MAX_FRAME_SIZE);
	return 0;
}

static void useeplus_buf_queue(struct vb2_buffer *vb)
{
	struct usb_useeplus *dev = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *v4l2_buf = to_vb2_v4l2_buffer(vb);
	struct useeplus_buffer *buf = container_of(v4l2_buf, struct useeplus_buffer, vb);
	unsigned long flags;

	spin_lock_irqsave(&dev->q_lock, flags);
	list_add_tail(&buf->list, &dev->rdy_queue);
	spin_unlock_irqrestore(&dev->q_lock, flags);
}

static void useeplus_kill_urbs(struct usb_useeplus *dev)
{
	int i;

	// Instantly signal to all incoming callbacks to drop resubmission
	clear_bit(FLAG_STREAMING, &dev->flags);
	smp_mb(); // Memory barrier to force all CPU cores to see this write

	// Kill ALL URBs first. This guarantees every callback is stopped 
	// and no new ones can be submitted.
	for (i = 0; i < BULK_TRANSFER_COUNT; ++i) {
		if (dev->urbs[i]) {
			usb_kill_urb(dev->urbs[i]);
		}
	}

	// Safe Zone: No callbacks can possibly be running now. 
	// It is now 100% safe to free the memory structures.
	for (i = 0; i < BULK_TRANSFER_COUNT; ++i) {
		if (dev->urbs[i]) {
			if (dev->urb_buffers[i]) {
				usb_free_coherent(dev->udev, BULK_TRANSFER_SIZE, 
								  dev->urb_buffers[i], dev->urb_dma_addrs[i]);
				dev->urb_buffers[i] = NULL;
			}
			usb_free_urb(dev->urbs[i]);
			dev->urbs[i] = NULL;
		}
	}
}

static int useeplus_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct usb_useeplus *dev = vb2_get_drv_priv(vq);
	unsigned long flags;
	int i, retval;

	spin_lock_irqsave(&dev->q_lock, flags);
	dev->current_frame_len = 0;
	dev->last_frame_id = -1;
	dev->has_stored_header = false;
	dev->vb_streaming = true;
	spin_unlock_irqrestore(&dev->q_lock, flags);

	/* 1. Set the atomic bit flag for the URB callbacks */
	set_bit(FLAG_STREAMING, &dev->flags);
	smp_mb(); /* Force visibility across all CPU cores */

	/* 2. Launch the URB pipeline */
	for (i = 0; i < BULK_TRANSFER_COUNT; ++i) {
		retval = usb_submit_urb(dev->urbs[i], GFP_KERNEL);
		if (retval) {
			dev_err(&dev->interface->dev, 
					"Failed to submit URB %d on stream start: %d\n", i, retval);
			goto error_submit;
		}
	}

	return 0;

error_submit:
	/* If launching fails mid-way, cleanly shut down whatever URBs started */
	useeplus_kill_urbs(dev);
	
	/* Turn off the V4L2 streaming state we just set */
	spin_lock_irqsave(&dev->q_lock, flags);
	dev->vb_streaming = false;
	spin_unlock_irqrestore(&dev->q_lock, flags);
	
	return retval;
}

static void useeplus_stop_streaming(struct vb2_queue *vq)
{
	struct usb_useeplus *dev = vb2_get_drv_priv(vq);
	struct useeplus_buffer *buf;
	unsigned long flags;

	/* 1. Stop and flush the USB pipeline FIRST (Safe to sleep here) */
	useeplus_kill_urbs(dev);

	/* 2. Lock to safely alter V4L2 states and flush the queue */
	spin_lock_irqsave(&dev->q_lock, flags);
	dev->vb_streaming = false;

	while (!list_empty(&dev->rdy_queue)) {
		buf = list_first_entry(&dev->rdy_queue, struct useeplus_buffer, list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}

	spin_unlock_irqrestore(&dev->q_lock, flags);
}

static const struct vb2_ops useeplus_vb2_ops = {
	.queue_setup	= useeplus_queue_setup,
	.buf_prepare	= useeplus_buf_prepare,
	.buf_queue	  = useeplus_buf_queue,
	.start_streaming = useeplus_start_streaming,
	.stop_streaming = useeplus_stop_streaming,
	.wait_prepare   = vb2_ops_wait_prepare,
	.wait_finish	= vb2_ops_wait_finish,
};

static int useeplus_v4l2_open(struct file *file)
{
	return v4l2_fh_open(file);
}

static int useeplus_v4l2_release(struct file *file)
{
	return _vb2_fop_release(file, NULL);
}

static const struct v4l2_file_operations useeplus_v4l2_fops = {
	.owner			= THIS_MODULE,
	.open			= useeplus_v4l2_open,
	.release		= useeplus_v4l2_release,
	.read			= vb2_fop_read,
	.poll			= vb2_fop_poll,
	.mmap			= vb2_fop_mmap,
	.unlocked_ioctl	= video_ioctl2,
};

static int useeplus_vidioc_querycap(struct file *file, void *priv, struct v4l2_capability *cap)
{
	struct usb_useeplus *dev = video_drvdata(file);

	strscpy(cap->driver, "Useeplus", sizeof(cap->driver));
	strscpy(cap->card, "Useeplus non-UVC Borescope", sizeof(cap->card));
	usb_make_path(dev->udev, cap->bus_info, sizeof(cap->bus_info));
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_DEVICE_CAPS;
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	return 0;
}

static int useeplus_vidioc_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
	f->fmt.pix.width		= 640;
	f->fmt.pix.height		= 480;
	f->fmt.pix.pixelformat	= V4L2_PIX_FMT_MJPEG;
	f->fmt.pix.field		= V4L2_FIELD_NONE;
	f->fmt.pix.bytesperline	= 0;
	f->fmt.pix.sizeimage	= MAX_FRAME_SIZE;
	f->fmt.pix.colorspace	= V4L2_COLORSPACE_SRGB;
	return 0;
}

static int useeplus_vidioc_enum_fmt_vid_cap(struct file *file, void *priv, struct v4l2_fmtdesc *f)
{
	if (f->index > 0)
		return -EINVAL;

	f->pixelformat = V4L2_PIX_FMT_MJPEG;
	return 0;
}

static int useeplus_vidioc_enum_input(struct file *file, void *priv, struct v4l2_input *inp)
{
	if (inp->index > 0)
		return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	strscpy(inp->name, "Borescope Lens Channel 0", sizeof(inp->name));
	return 0;
}

static int useeplus_vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int useeplus_vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	return i == 0 ? 0 : -EINVAL;
}

static int useeplus_vidioc_g_parm(struct file *file, void *priv, struct v4l2_streamparm *sp)
{
	if (sp->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	sp->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	sp->parm.capture.timeperframe.numerator = 1;
	sp->parm.capture.timeperframe.denominator = 30;
	return 0;
}

static int useeplus_vidioc_s_parm(struct file *file, void *priv, struct v4l2_streamparm *sp)
{
	return useeplus_vidioc_g_parm(file, priv, sp);
}

static const struct v4l2_ioctl_ops useeplus_v4l2_ioctl_ops = {
	.vidioc_querycap			= useeplus_vidioc_querycap,
	.vidioc_g_fmt_vid_cap		= useeplus_vidioc_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= useeplus_vidioc_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= useeplus_vidioc_fmt_vid_cap,
	.vidioc_enum_fmt_vid_cap	= useeplus_vidioc_enum_fmt_vid_cap,
	.vidioc_enum_input			= useeplus_vidioc_enum_input,
	.vidioc_g_input				= useeplus_vidioc_g_input,
	.vidioc_s_input				= useeplus_vidioc_s_input,
	.vidioc_g_parm				= useeplus_vidioc_g_parm,
	.vidioc_s_parm				= useeplus_vidioc_s_parm,
	.vidioc_reqbufs				= vb2_ioctl_reqbufs,
	.vidioc_querybuf			= vb2_ioctl_querybuf,
	.vidioc_qbuf				= vb2_ioctl_qbuf,
	.vidioc_dqbuf				= vb2_ioctl_dqbuf,
	.vidioc_streamon			= vb2_ioctl_streamon,
	.vidioc_streamoff			= vb2_ioctl_streamoff,
};

static int useeplus_write_msg(struct usb_useeplus *dev, u8 endpoint_addr, const u8 *tokens, size_t len)
{
	int retval;
	int actual_length;
	u8 *dma_buffer;

	dma_buffer = kmemdup(tokens, len, GFP_KERNEL);
	if (!dma_buffer)
		return -ENOMEM;

	retval = usb_bulk_msg(dev->udev,
				  usb_sndbulkpipe(dev->udev, endpoint_addr),
				  dma_buffer,
				  len,
				  &actual_length,
				  USB_TIMEOUT_MS);

	kfree(dma_buffer);
	return retval;
}

static void useeplus_read_bulk_callback(struct urb *urb)
{
	struct usb_useeplus *dev = urb->context;
	struct useeplus_buffer *vbuf;
	size_t i = 0;
	unsigned long flags;
	int retval;

	if (urb->status) {
		switch (urb->status) {
		case -ENOENT:
		case -ECONNRESET:
		case -ESHUTDOWN:
		case -ENODEV:
			dev_dbg(&urb->dev->dev, "URB stopped cleanly: %d\n", urb->status);
			return;

		case -EPROTO:
		case -EILSEQ:
		case -ECOMM:
			dev_dbg(&urb->dev->dev, "Transient CRC/timeout error: %d. Retrying...\n", urb->status);
			goto resubmit;

		case -EPIPE:
			dev_err(&urb->dev->dev, "Endpoint stalled. Clear halt required.\n");
			// Optional: Schedule a workqueue to call usb_clear_halt()
			return; 

		default:
			dev_err(&urb->dev->dev, "Uncaught URB error: %d. Aborting stream.\n", urb->status);
			return;
		}
	}

	dev->dbg_urbs_processed++;

	if (dev->dbg_urbs_processed % 300 == 0) {
		dev_dbg(&dev->interface->dev,
			"DIAGNOSTIC DUMP | URBs: %lu | Packets: %lu | Frames: %lu (Delivered: %lu | Drop SOI: %lu | Drop EOI: %lu | Drop Q: %lu | Ghosts: %lu)\n",
			dev->dbg_urbs_processed, dev->dbg_packets_found, dev->dbg_frames_found,
			dev->dbg_frames_delivered, dev->dbg_frames_dropped_soi, dev->dbg_frames_dropped_eoi,
			dev->dbg_frames_dropped_queue, dev->dbg_ghost_headers);
	}

	if (dev->parse_len + urb->actual_length > BULK_TRANSFER_SIZE * 2) {
		dev_warn(&dev->interface->dev, "Parse buffer overflow, dropping data\n");
		dev->parse_len = 0;
	} else {
		memcpy(dev->parse_buffer + dev->parse_len, urb->transfer_buffer, urb->actual_length);
		dev->parse_len += urb->actual_length;
	}

	while (i + TOTAL_PROTOCOL_HEADER_SIZE <= dev->parse_len) {

		uint16_t current_magic = get_unaligned_le16(dev->parse_buffer + i);
		u8 current_camera_id = dev->parse_buffer[i + 2];
		uint16_t packet_len = get_unaligned_le16(dev->parse_buffer + i + 3);

		if (current_magic != PROTO_FRAME_HEADER_MAGIC ||
			(current_camera_id != PROTO_VIDEO_CAMERA_ID &&
			current_camera_id != PROTO_GRAVITY_CAMERA_ID)) {
			i++;
			continue;
		}

		{
			bool isGhost = false;
			size_t nextHeaderOffset = 0;
			size_t maxScan = min((size_t)MAX_SCAN_LIMIT, (size_t)(dev->parse_len - i - 3));
			size_t d;

			for (d = NATIVE_PACKET_HEADER_SIZE; d <= maxScan; ++d) {
				if (dev->parse_buffer[i + d] == PROTO_FRAME_HEADER_A &&
					dev->parse_buffer[i + d + 1] == PROTO_FRAME_HEADER_B &&
					(dev->parse_buffer[i + d + 2] == PROTO_VIDEO_CAMERA_ID ||
					dev->parse_buffer[i + d + 2] == PROTO_GRAVITY_CAMERA_ID)) {
					isGhost = true;
					nextHeaderOffset = d;
					break;
				}
			}

			if (isGhost) {
				dev->dbg_ghost_headers++;
				i += nextHeaderOffset;
				continue;
			}
		}

		dev->dbg_packets_found++;
		size_t totalPacketSize = NATIVE_PACKET_HEADER_SIZE + packet_len;

		if (i + totalPacketSize > dev->parse_len)
			break;

		if (packet_len < NATIVE_PAYLOAD_HEADER_SIZE) {
			i += totalPacketSize;
			continue;
		}

		size_t payload_offset = i + NATIVE_PACKET_HEADER_SIZE;
		u8 current_frame_id = dev->parse_buffer[payload_offset];
		u8 current_camera_number = dev->parse_buffer[payload_offset + 1];
		u8 current_flags = dev->parse_buffer[payload_offset + 2];

		if (dev->has_stored_header && dev->current_frame_len > 0 &&
			dev->last_frame_id != current_frame_id) {

			dev->dbg_frames_found++;
			size_t soiOffset = 0;
			size_t eoiOffset = 0;
			size_t j;
			bool found_soi = false;
			bool found_eoi = false;

			for (j = 0; j + 1 < min((size_t)256, dev->current_frame_len); ++j) {
				if (dev->current_frame[j] == 0xFF && dev->current_frame[j + 1] == 0xD8) {
					soiOffset = j;
					found_soi = true;
					break;
				}
			}

			for (j = dev->current_frame_len; j >= 2; --j) {
				if (dev->current_frame[j - 2] == 0xFF && dev->current_frame[j - 1] == 0xD9) {
					eoiOffset = j;
					found_eoi = true;
					break;
				}
			}

			if (!found_soi) {
				dev->dbg_frames_dropped_soi++;
			} else if (!found_eoi || eoiOffset <= soiOffset) {
				dev->dbg_frames_dropped_eoi++;
			} else {
				size_t final_content_size = eoiOffset - soiOffset;

				dev->frame_counter++;

				spin_lock_irqsave(&dev->q_lock, flags);
				if (dev->vb_streaming && !list_empty(&dev->rdy_queue)) {
					vbuf = list_first_entry(&dev->rdy_queue, struct useeplus_buffer, list);
					list_del(&vbuf->list);
					void *vaddr = vb2_plane_vaddr(&vbuf->vb.vb2_buf, 0);

					if (vaddr) {
						memcpy(vaddr, dev->current_frame + soiOffset, final_content_size);
						vb2_set_plane_payload(&vbuf->vb.vb2_buf, 0, final_content_size);
						vbuf->vb.vb2_buf.timestamp = ktime_get_ns();
						vbuf->vb.sequence = dev->sequence++;
						vbuf->vb.field = V4L2_FIELD_NONE;
						vb2_buffer_done(&vbuf->vb.vb2_buf, VB2_BUF_STATE_DONE);
						dev->dbg_frames_delivered++;
					} else {
						vb2_buffer_done(&vbuf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
						dev->dbg_frames_dropped_queue++;
					}
				} else {
					dev->dbg_frames_dropped_queue++;
				}
				spin_unlock_irqrestore(&dev->q_lock, flags);
			}
			dev->current_frame_len = 0;
		}

		dev->last_frame_id = current_frame_id;
		dev->has_stored_header = true;

		bool hasGravitySensor = (current_flags & 0x01) != 0;
		uint8_t otherFlags = (current_flags >> 2) & 0x3F;

		if (!hasGravitySensor && otherFlags == 0 && current_camera_number < 2) {
			size_t payloadStart = i + TOTAL_PROTOCOL_HEADER_SIZE;
			size_t payloadSize = totalPacketSize - TOTAL_PROTOCOL_HEADER_SIZE;

			if (dev->current_frame_len + payloadSize <= MAX_FRAME_SIZE) {
				memcpy(dev->current_frame + dev->current_frame_len,
					   dev->parse_buffer + payloadStart, payloadSize);
				dev->current_frame_len += payloadSize;
			}
		}

		i += totalPacketSize;
	}

	if (i < dev->parse_len) {
		size_t remaining = dev->parse_len - i;

		memmove(dev->parse_buffer, dev->parse_buffer + i, remaining);
		dev->parse_len = remaining;
	} else {
		dev->parse_len = 0;
	}

resubmit:
	if (test_bit(FLAG_STREAMING, &dev->flags)) {
		retval = usb_submit_urb(urb, GFP_ATOMIC);
		if (retval) {
			dev_err(&dev->interface->dev, "Resubmit failed: %d\n", retval);
		}
	}
}

static int useeplus_probe(struct usb_interface *interface,
						  const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_useeplus *dev = NULL;
	struct vb2_queue *q;
	u8 *drain_buffer;
	int i, retval, actual_len;

	if (interface->cur_altsetting->desc.bInterfaceNumber != 1)
		return -ENODEV;

	dev_info(&interface->dev,
			 "Useeplus borescope identified.\n");

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->udev = udev;
	dev->interface = interface;
	dev->sequence = 0;
	dev->vb_streaming = false;
	dev->has_stored_header = false;
	dev->current_frame_len = 0;
	dev->parse_len = 0;

	mutex_init(&dev->v4l2_lock);
	spin_lock_init(&dev->q_lock);
	INIT_LIST_HEAD(&dev->rdy_queue);

	dev->current_frame = vzalloc(MAX_FRAME_SIZE);
	if (!dev->current_frame) {
		retval = -ENOMEM;
		goto error;
	}

	dev->parse_buffer = kzalloc(BULK_TRANSFER_SIZE * 2, GFP_KERNEL);
	if (!dev->parse_buffer) {
		retval = -ENOMEM;
		goto error;
	}

	retval = v4l2_device_register(&interface->dev, &dev->v4l2_dev);
	if (retval)
		goto error;

	q = &dev->vb_vidq;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_READ;
	q->drv_priv = dev;
	q->buf_struct_size = sizeof(struct useeplus_buffer);
	q->ops = &useeplus_vb2_ops;
	q->mem_ops = &vb2_vmalloc_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->min_queued_buffers = 2;
	q->lock = &dev->v4l2_lock;
	q->dev = &interface->dev;
	strscpy(q->name, "useeplus-queue", sizeof(q->name));

	retval = vb2_queue_init(q);
	if (retval) {
		dev_err(&interface->dev, "vb2_queue_init failed\n");
		goto error_unreg_v4l2;
	}

	strscpy(dev->vdev.name, "useeplus-video", sizeof(dev->vdev.name));
	dev->vdev.v4l2_dev = &dev->v4l2_dev;
	dev->vdev.fops = &useeplus_v4l2_fops;
	dev->vdev.ioctl_ops = &useeplus_v4l2_ioctl_ops;
	dev->vdev.release = video_device_release_empty;
	dev->vdev.lock = &dev->v4l2_lock;
	dev->vdev.queue = q;
	dev->vdev.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	video_set_drvdata(&dev->vdev, dev);

	drain_buffer = kmalloc(512, GFP_KERNEL);
	if (!drain_buffer) {
		retval = -ENOMEM;
		goto error_unreg_v4l2;
	}

	for (i = 0; i < 30; ++i) {
		usb_bulk_msg(udev, usb_rcvbulkpipe(udev, 0x82), drain_buffer, 512,
					 &actual_len, 100);
	}
	kfree(drain_buffer);

	retval = usb_set_interface(udev, 1, 1);
	if (retval)
		goto error_unreg_v4l2;

	usb_clear_halt(udev, usb_rcvbulkpipe(udev, 0x81));

	for (i = 0; i < BULK_TRANSFER_COUNT; ++i) {
		dev->urbs[i] = usb_alloc_urb(0, GFP_KERNEL);
		if (!dev->urbs[i]) {
			retval = -ENOMEM;
			goto error_urbs;
		}
		dev->urb_buffers[i] = usb_alloc_coherent(
			udev, BULK_TRANSFER_SIZE, GFP_KERNEL, &dev->urb_dma_addrs[i]);
		if (!dev->urb_buffers[i]) {
			retval = -ENOMEM;
			goto error_urbs;
		}
		usb_fill_bulk_urb(dev->urbs[i], udev, usb_rcvbulkpipe(udev, 0x81),
						  dev->urb_buffers[i], BULK_TRANSFER_SIZE,
						  useeplus_read_bulk_callback, dev);
		dev->urbs[i]->transfer_dma = dev->urb_dma_addrs[i];
		dev->urbs[i]->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	}

	usb_set_intfdata(interface, dev);

	retval = useeplus_write_msg(dev, 0x02, initialization_tokens, sizeof(initialization_tokens));
	if (retval)
		goto error_sequence;

	retval = useeplus_write_msg(dev, 0x01, start_stream_tokens, sizeof(start_stream_tokens));
	if (retval)
		goto error_sequence;

	retval = video_register_device(&dev->vdev, VFL_TYPE_VIDEO, -1);
	if (retval)
		goto error_sequence;

	dev_info(
		&interface->dev,
		"Useeplus protocol borescope connected successfully.\n");

	/* Atomically set the streaming bit to true */
    set_bit(FLAG_STREAMING, &dev->flags);
    
    /* Force all CPU cores to immediately see this flag change */
    smp_mb(); 

	for (i = 0; i < BULK_TRANSFER_COUNT; ++i) {
		retval = usb_submit_urb(dev->urbs[i], GFP_KERNEL);
		if (retval)
			goto error_unreg_video;
	}
	return 0;

error_unreg_video:
	video_unregister_device(&dev->vdev);
error_sequence:
	useeplus_kill_urbs(dev);
	usb_set_intfdata(interface, NULL);
error_urbs:
	useeplus_kill_urbs(dev);
error_unreg_v4l2:
	v4l2_device_unregister(&dev->v4l2_dev);
error:
	if (dev) {
		if (dev->current_frame)
			vfree(dev->current_frame);

		kfree(dev->parse_buffer);
		kfree(dev);
	}
	return retval;
}

static void useeplus_disconnect(struct usb_interface *interface)
{
	struct usb_useeplus *dev = usb_get_intfdata(interface);

	usb_set_intfdata(interface, NULL);

	if (dev) {
		useeplus_kill_urbs(dev);
		video_unregister_device(&dev->vdev);
		v4l2_device_unregister(&dev->v4l2_dev);

		if (dev->current_frame)
			vfree(dev->current_frame);

		kfree(dev->parse_buffer);

		dev_info(&interface->dev, "Useeplus protocol borescope detached.\n");
		kfree(dev);
	}
}

static struct usb_driver useeplus_driver = {
	.name	   = "useeplus",
	.id_table   = useeplus_table,
	.probe	  = useeplus_probe,
	.disconnect = useeplus_disconnect,
};

static int __init useeplus_init(void)
{
	pr_debug("useeplus_v4l2: Module initialized. Version: %s\n", BUILD_VER);
	return usb_register(&useeplus_driver);
}

static void __exit useeplus_exit(void)
{
	pr_debug("useeplus_v4l2: Module exited.\n");
	usb_deregister(&useeplus_driver);
}

module_init(useeplus_init);
module_exit(useeplus_exit);
