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

#define USB_TIMEOUT_MS						1000
#define BULK_TRANSFER_COUNT					4

#define BULK_TRANSFER_SIZE					16384
#define MAX_FRAME_SIZE						(256 * 1024)

#define MAX_SCAN_LIMIT						160

#define USB_PACKET_HEADER_SIZE				5
#define USB_PAYLOAD_HEADER_SIZE				7
#define TOTAL_USB_HEADER_SIZE				(USB_PACKET_HEADER_SIZE + USB_PAYLOAD_HEADER_SIZE)
#define MAX_SCAN_LIMIT						160

#define USEEPLUS_IAP_INTERFACE				0
#define USEEPLUS_VIDEO_INTERFACE			1
#define USEEPLUS_ALT_SETTING_VIDEO_ENABLE	1

#define USEEPLUS_VIDEO_ENDPOINT				0x01
#define USEEPLUS_IAP_ENDPOINT				0x02

#define HEARTBEAT_SINK_BUFFER_SIZE			512
#define HEARTBEAT_SINK_ITERATIONS			30
#define HEARTBEAT_SINK_TIMEOUT_MS			100

#define JPEG_SOI_MARKERS_MAX_POSITION		256
#define VIDEO_CAMERA_ID						0x0B
#define GRAVITY_SENSOR_ID					0x07

#define USB_PACKET_DELIMETER				0xBBAA
#define USB_PACKET_DELIMETER_A				0xAA
#define USB_PACKET_DELIMETER_B				0xBB
#define BOUNDARY_MARKER						0xFF
#define START_MARKER						0xD8
#define END_MARKER							0xD9

#define RESOLUTION_WIDTH					640
#define RESOLUTION_HEIGHT					480
#define DIAGNOSTIC_LOG_ITERATIONS			300

#define FLAG_STREAMING						0

static const u8 IAP_AUTH_HANDSHAKE[]	= { 0xFF, 0x55, 0xFF, 0x55, 0xEE, 0x10 };
static const u8 START_VIDEO_COMMAND[]	= { 0xBB, 0xAA, 0x05, 0x00, 0x00 };

static const struct usb_device_id useeplus_table[] = {
	{ USB_DEVICE(0x0329, 0x2022) },
	{ USB_DEVICE(0x2ce3, 0x3828) },
	{ }
};
MODULE_DEVICE_TABLE(usb, useeplus_table);

struct usb_packet_header {
	__le16 le_delimeter;
	u8 le_device_id;
	__le16 le_length;
} __packed;

struct usb_payload_header {
	u8 le_frame_id;
	u8 le_camera_number;
	u8 le_flags;
	__le32 le_gravity_sensor;
} __packed;

struct useeplus_buffer {
	struct vb2_v4l2_buffer vb2_buffer;
	struct list_head list;
};

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

	unsigned long flags;
	bool streaming_video;

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
	unsigned long dbg_usb_errors;
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
	struct useeplus_drv_data *drv_data = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *v4l2_buf = to_vb2_v4l2_buffer(vb);
	struct useeplus_buffer *buf = container_of(v4l2_buf, struct useeplus_buffer, vb);
	unsigned long flags;

	spin_lock_irqsave(&drv_data->ready_queue_lock, flags);
	list_add_tail(&buf->list, &drv_data->ready_queue);
	spin_unlock_irqrestore(&drv_data->ready_queue_lock, flags);
}

static void useeplus_kill_urbs(struct useeplus_drv_data *drv_data)
{
	clear_bit(FLAG_STREAMING, &drv_data->flags);

	// Kill ALL URBs first. This guarantees every callback is stopped
	// and no new ones can be submitted.
	for (int i = 0; i < BULK_TRANSFER_COUNT; ++i) {
		if (drv_data->urbs[i])
			usb_kill_urb(drv_data->urbs[i]);
	}

	// Safe Zone: No callbacks can possibly be running now.
	// It is now 100% safe to free the memory structures.
	for (int i = 0; i < BULK_TRANSFER_COUNT; ++i) {
		if (drv_data->urbs[i]) {
			if (drv_data->urb_buffers[i]) {
				usb_free_coherent(ddrv_data->usb_dev, BULK_TRANSFER_SIZE,
								  drv_data->urb_buffers[i], drv_data->urb_dma_addrs[i]);
				drv_data->urb_buffers[i] = NULL;
			}
			usb_free_urb(drv_data->urbs[i]);
			drv_data->urbs[i] = NULL;
		}
	}
}

static int useeplus_write_msg(struct useeplus_drv_data *drv_data, u8 endpoint_addr, const u8 *tokens, size_t len)
{
	int retval;
	int actual_length;
	u8 *dma_buffer;

	dma_buffer = kmemdup(tokens, len, GFP_KERNEL);
	if (!dma_buffer)
		return -ENOMEM;

	retval = usb_bulk_msg(drv_data->usb_dev,
				  usb_sndbulkpipe(drv_data->usb_dev, endpoint_addr),
				  dma_buffer,
				  len,
				  &actual_length,
				  USB_TIMEOUT_MS);

	kfree(dma_buffer);
	return retval;
}

static int useeplus_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct useeplus_drv_data *drv_data = vb2_get_drv_priv(vq);
	unsigned long flags;
	int urbs_submitted, retval;

	spin_lock_irqsave(&drv_data->ready_queue_lock, flags);
	drv_data->current_frame_len = 0;
	drv_data->last_frame_id = -1;
	drv_data->has_stored_header = false;
	drv_data->streaming_video = true;
	spin_unlock_irqrestore(&drv_data->ready_queue_lock, flags);
	if (test_and_set_bit(FLAG_STREAMING, &drv_data->flags))
		return 0;

	retval = useeplus_write_msg(dev, drv_data->iap_out_ep, IAP_AUTH_HANDSHAKE, sizeof(IAP_AUTH_HANDSHAKE));
	if (retval) {
		dev_err(&drv_data->interface->dev, "useeplus_write_msg init failed: %d\n", retval);
		goto error_start;
	}

	retval = useeplus_write_msg(dev, drv_data->video_out_ep, START_VIDEO_COMMAND, sizeof(START_VIDEO_COMMAND));
	if (retval) {
		dev_err(&drv_data->interface->dev, "useeplus_write_msg start failed: %d\n", retval);
		goto error_start;
	}

	/* Ensure memory visibility before hardware DMA triggers */
	smp_mb__after_atomic();

	/* Submit URBs to begin pulling the stream */
	for (urbs_submitted = 0; urbs_submitted < BULK_TRANSFER_COUNT; ++urbs_submitted) {
		retval = usb_submit_urb(drv_data->urbs[urbs_submitted], GFP_KERNEL);
		if (retval) {
			dev_err(&drv_data->interface->dev, "Failed to submit URBs: %d\n", retval);
			goto error_start;
		}
	}

	return 0;

error_start:
	clear_bit(FLAG_STREAMING, &drv_data->flags);

	/* Kill any URBs that successfully submitted before the failure */
	for (int i = 0; i < urbs_submitted; ++i)
		usb_kill_urb(drv_data->urbs[i]);

	/* Drain the queue and return to userspace per V4L2 spec */
	spin_lock_irqsave(&drv_data->ready_queue_lock, flags);
	drv_data->streaming_video = false;

	while (!list_empty(&drv_data->ready_queue)) {
		struct useeplus_buffer *buf;

		buf = list_first_entry(&drv_data->ready_queue, struct useeplus_buffer, list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb2_buffer.vb2_buf, VB2_BUF_STATE_QUEUED);
	}
	spin_unlock_irqrestore(&drv_data->ready_queue_lock, flags);

	return retval;
}

static void useeplus_stop_streaming(struct vb2_queue *vq)
{
	struct useeplus_drv_data *drv_data = vb2_get_drv_priv(vq);
	struct useeplus_buffer *buf;
	unsigned long flags;

	clear_bit(FLAG_STREAMING, &drv_data->flags);

	for (int i = 0; i < BULK_TRANSFER_COUNT; ++i) {
		if (drv_data->urbs[i])
			usb_kill_urb(drv_data->urbs[i]);
	}

	spin_lock_irqsave(&drv_data->ready_queue_lock, flags);
	drv_data->streaming_video = false;

	while (!list_empty(&drv_data->ready_queue)) {
		buf = list_first_entry(&drv_data->ready_queue, struct useeplus_buffer, list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb2_buffer.vb2_buf, VB2_BUF_STATE_ERROR);
	}

	spin_unlock_irqrestore(&drv_data->ready_queue_lock, flags);
}

static const struct vb2_ops useeplus_vb2_ops = {
	.queue_setup		= useeplus_queue_setup,
	.buf_prepare		= useeplus_buf_prepare,
	.buf_queue			= useeplus_buf_queue,
	.start_streaming	= useeplus_start_streaming,
	.stop_streaming		= useeplus_stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
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
	struct useeplus_drv_data *drv_data = video_drvdata(file);

	strscpy(cap->driver, "Useeplus", sizeof(cap->driver));
	strscpy(cap->card, "Useeplus non-UVC Borescope", sizeof(cap->card));
	usb_make_path(drv_data->usb_dev, cap->bus_info, sizeof(cap->bus_info));
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_DEVICE_CAPS;
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;

	return 0;
}

static int useeplus_vidioc_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
	f->fmt.pix.width		= RESOLUTION_WIDTH;
	f->fmt.pix.height		= RESOLUTION_HEIGHT;
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

static void useeplus_read_bulk_callback(struct urb *urb)
{
	struct useeplus_drv_data *drv_data = urb->context;
	struct useeplus_buffer *vbuf;
	size_t current_parse_index = 0;
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
			drv_data->dbg_usb_errors++;
			goto resubmit;
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

	drv_data->dbg_urbs_processed++;

	if (drv_data->dbg_urbs_processed % DIAGNOSTIC_LOG_ITERATIONS == 0) {
		dev_dbg(&drv_data->interface->dev,
			"DIAGNOSTIC DUMP | URBs: %lu | USB Errors: %lu| Packets: %lu | Frames: %lu (Delivered: %lu | Drop SOI: %lu | Drop EOI: %lu | Drop Q: %lu | Ghosts: %lu)\n",
			drv_data->dbg_urbs_processed, drv_data->dbg_usb_errors, drv_data->dbg_packets_found, drv_data->dbg_frames_found,
			drv_data->dbg_frames_delivered, drv_data->dbg_frames_dropped_soi, drv_data->dbg_frames_dropped_eoi,
			drv_data->dbg_frames_dropped_queue, drv_data->dbg_ghost_headers);
	}

	if (drv_data->parse_len + urb->actual_length > BULK_TRANSFER_SIZE * 2) {
		dev_warn(&drv_data->interface->dev, "Parse buffer overflow, dropping data\n");
		drv_data->parse_len = 0;
	} else {
		memcpy(drv_data->parse_buffer + drv_data->parse_len, urb->transfer_buffer, urb->actual_length);
		drv_data->parse_len += urb->actual_length;
	}

	while (current_parse_index + TOTAL_USB_HEADER_SIZE <= drv_data->parse_len) {
		struct usb_packet_header *pkt = (struct usb_packet_header *)(drv_data->parse_buffer + current_parse_index);
		uint16_t delimeter = le16_to_cpu(pkt->le_delimeter);
		u8 camera_id = pkt->le_device_id;
		uint16_t packet_len = le16_to_cpu(pkt->le_length);

		if (delimeter != USB_PACKET_DELIMETER ||
			(camera_id != VIDEO_CAMERA_ID &&
			 camera_id != GRAVITY_SENSOR_ID)) {
			current_parse_index++;
			continue;
		}

		{
			bool is_ghost = false;
			size_t header_offset = 0;
			size_t max_scan = min_t(size_t, MAX_SCAN_LIMIT, drv_data->parse_len - current_parse_index - 3);

			for (size_t offset = USB_PACKET_HEADER_SIZE; offset <= max_scan; ++offset) {
				struct usb_packet_header *offset_pkt =
					(struct usb_packet_header *)(drv_data->parse_buffer + current_parse_index + offset);

				uint16_t offset_delimeter = le16_to_cpu(offset_pkt->le_delimeter);
				u8 offset_camera_id = offset_pkt->le_device_id;

				if (offset_delimeter == USB_PACKET_DELIMETER &&
					(offset_camera_id == VIDEO_CAMERA_ID ||
					 offset_camera_id == GRAVITY_SENSOR_ID)) {
					is_ghost = true;
					header_offset = offset;
					break;
				}
			}

			if (is_ghost) {
				drv_data->dbg_ghost_headers++;
				current_parse_index += header_offset;
				continue;
			}
		}

		drv_data->dbg_packets_found++;
		size_t total_packet_size = USB_PACKET_HEADER_SIZE + packet_len;

		if (total_packet_size > (BULK_TRANSFER_SIZE * 2)) {
			dev_dbg(&drv_data->interface->dev, "Corrupted packet_len %u, skipping byte\n", packet_len);
			current_parse_index++;
			continue;
		}

		if (current_parse_index + total_packet_size > drv_data->parse_len)
			break;

		if (packet_len < USB_PAYLOAD_HEADER_SIZE) {
			current_parse_index += total_packet_size;
			continue;
		}

		size_t payload_offset = current_parse_index + USB_PACKET_HEADER_SIZE;

		struct usb_payload_header *payload = (struct usb_payload_header *)(drv_data->parse_buffer + payload_offset);

		u8 current_frame_id = payload->le_frame_id;
		u8 current_camera_number = payload->le_camera_number;
		u8 current_flags = payload->le_flags;

		if (drv_data->has_stored_header && drv_data->current_frame_len > 0 &&
			drv_data->last_frame_id != current_frame_id) {

			drv_data->dbg_frames_found++;
			size_t soi_offset = 0;
			size_t eoi_offset = 0;
			bool found_soi = false;
			bool found_eoi = false;

			for (size_t j = 0; j + 1 < min_t(size_t, JPEG_SOI_MARKERS_MAX_POSITION, drv_data->current_frame_len); ++j) {
				if (drv_data->current_frame[j] == BOUNDARY_MARKER && drv_data->current_frame[j + 1] == START_MARKER) {
					soi_offset = j;
					found_soi = true;
					break;
				}
			}

			for (size_t j = drv_data->current_frame_len; j >= 2; --j) {
				if (drv_data->current_frame[j - 2] == BOUNDARY_MARKER && drv_data->current_frame[j - 1] == END_MARKER) {
					eoi_offset = j;
					found_eoi = true;
					break;
				}
			}

			if (!found_soi) {
				drv_data->dbg_frames_dropped_soi++;
			} else if (!found_eoi || eoi_offset <= soi_offset) {
				drv_data->dbg_frames_dropped_eoi++;
			} else {
				size_t final_content_size = eoi_offset - soi_offset;

				drv_data->frame_counter++;

				spin_lock_irqsave(&drv_data->ready_queue_lock, flags);
				if (drv_data->streaming_video && !list_empty(&drv_data->ready_queue)) {
					vbuf = list_first_entry(&drv_data->ready_queue, struct useeplus_buffer, list);
					list_del(&vbuf->list);
					void *vaddr = vb2_plane_vaddr(&vbuf->vb2_buffer.vb2_buf, 0);

					if (vaddr) {
						memcpy(vaddr, drv_data->current_frame + soi_offset, final_content_size);
						vb2_set_plane_payload(&vbuf->vb2_buffer.vb2_buf, 0, final_content_size);
						vbuf->vb2_buffer.vb2_buf.timestamp = ktime_get_ns();
						vbuf->vb2_buffer.sequence = drv_data->sequence++;
						vbuf->vb2_buffer.field = V4L2_FIELD_NONE;
						vb2_buffer_done(&vbuf->vb2_buffer.vb2_buf, VB2_BUF_STATE_DONE);
						drv_data->dbg_frames_delivered++;
					} else {
						vb2_buffer_done(&vbuf->vb2_buffer.vb2_buf, VB2_BUF_STATE_ERROR);
						drv_data->dbg_frames_dropped_queue++;
					}
				} else {
					drv_data->dbg_frames_dropped_queue++;
				}
				spin_unlock_irqrestore(&drv_data->ready_queue_lock, flags);
			}
			drv_data->current_frame_len = 0;
		}

		drv_data->last_frame_id = current_frame_id;
		drv_data->has_stored_header = true;

		bool has_gravity_sensor = (current_flags & 0x01) != 0;
		uint8_t other_flags = (current_flags >> 2) & 0x3F;

		if (!has_gravity_sensor && other_flags == 0 && current_camera_number < 2) {
			size_t payload_start = current_parse_index + TOTAL_USB_HEADER_SIZE;
			size_t payload_size = total_packet_size - TOTAL_USB_HEADER_SIZE;

			if (drv_data->current_frame_len + payload_size <= MAX_FRAME_SIZE) {
				memcpy(drv_data->current_frame + drv_data->current_frame_len,
					   drv_data->parse_buffer + payload_start, payload_size);
				drv_data->current_frame_len += payload_size;
			}
		}

		current_parse_index += total_packet_size;
	}

	if (current_parse_index < drv_data->parse_len) {
		size_t remaining = drv_data->parse_len - current_parse_index;

		memmove(drv_data->parse_buffer, drv_data->parse_buffer + current_parse_index, remaining);
		drv_data->parse_len = remaining;
	} else {
		drv_data->parse_len = 0;
	}

resubmit:
	if (test_bit(FLAG_STREAMING, &drv_data->flags)) {
		retval = usb_submit_urb(urb, GFP_ATOMIC);
		if (retval) {
			if (retval != -ENODEV && retval != -ESHUTDOWN && retval != -ENOENT)
				dev_err(&drv_data->interface->dev, "usb_submit_urb failed with error %d\n", retval);
			else
				dev_warn(&drv_data->interface->dev, "usb_submit_urb failed returned %d\n", retval);
		}
	}
}

static int useeplus_alloc_urbs(struct useeplus_drv_data *drv_data)
{
	struct usb_device *usb_dev = drv_data->usb_dev;
	struct usb_interface *interface = drv_data->interface;

	for (int i = 0; i < BULK_TRANSFER_COUNT; ++i) {
		drv_data->urbs[i] = usb_alloc_urb(0, GFP_KERNEL);
		if (!drv_data->urbs[i]) {
			dev_err(&interface->dev, "usb_alloc_urb failed\n");
			return -ENOMEM;
		}

		drv_data->urb_buffers[i] = usb_alloc_coherent(
			usb_dev,
			BULK_TRANSFER_SIZE,
			GFP_KERNEL,
			&drv_data->urb_dma_addrs[i]
		);

		if (!drv_data->urb_buffers[i]) {
			dev_err(&interface->dev, "usb_alloc_coherent failed\n");
			return -ENOMEM;
		}

		usb_fill_bulk_urb(
			drv_data->urbs[i],
			usb_dev,
			usb_rcvbulkpipe(usb_dev, drv_data->video_in_ep),
			drv_data->urb_buffers[i],
			BULK_TRANSFER_SIZE,
			useeplus_read_bulk_callback,
			dev
		);
		drv_data->urbs[i]->transfer_dma = drv_data->urb_dma_addrs[i];
		drv_data->urbs[i]->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	}
	return 0;
}

static int useeplus_suspend(struct usb_interface *intf, pm_message_t message)
{
	/* Stop URBs to prepare for sleep */
	return 0;
}

static int useeplus_resume(struct usb_interface *intf)
{
	/* Resubmit URBs if FLAG_STREAMING is true */
	return 0;
}

static void useeplus_disconnect(struct usb_interface *interface)
{
	struct useeplus_drv_data *drv_data = usb_get_intfdata(interface);

	usb_set_intfdata(interface, NULL);

	/* * Ignore the iAP interface disconnect.
	 * The Video Interface disconnect handles the full device teardown.
	 */
	if (interface->cur_altsetting->desc.bInterfaceNumber == USEEPLUS_IAP_INTERFACE)
		return;

	if (dev) {
		useeplus_kill_urbs(dev);

		/* Safely check if V4L2 actually registered before unregistering */
		if (video_is_registered(&drv_data->video_dev))
			video_unregister_device(&drv_data->video_dev);

		v4l2_device_disconnect(&drv_data->v4l2_dev);
		v4l2_device_put(&drv_data->v4l2_dev);
		dev_info(&interface->dev, "Useeplus protocol borescope detached.\n");
	}
}

static int useeplus_probe(struct usb_interface *interface, const struct usb_device_id *id);

static struct usb_driver useeplus_driver = {
	.name			= "useeplus",
	.id_table		= useeplus_table,
	.probe			= useeplus_probe,
	.disconnect		= useeplus_disconnect,
	.suspend		= useeplus_suspend,
	.resume			= useeplus_resume,
	.reset_resume	= useeplus_resume,
};

static void useeplus_device_release(struct v4l2_device *v4l2_dev)
{
	struct useeplus_drv_data *drv_data = container_of(v4l2_dev, struct useeplus_drv_data, v4l2_dev);

	if (drv_data->current_frame)
		vfree(drv_data->current_frame);

	kfree(drv_data->parse_buffer);
	kfree(dev);
}

static int useeplus_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_device *usb_dev = interface_to_usbdev(interface);
	struct usb_interface *iap_intf;
	struct usb_endpoint_descriptor *ep_desc;
	struct usb_host_interface *video_alt;
	struct useeplus_drv_data *drv_data = NULL;
	struct vb2_queue *q;
	u8 *iap_heartbeat_sink;
	int i, retval, actual_len;

	/* Only bind the driver when the Video Interface is probed */
	if (interface->cur_altsetting->desc.bInterfaceNumber != USEEPLUS_VIDEO_INTERFACE)
		return -ENODEV;

	dev_info(&interface->dev, "Useeplus borescope identified\n");

	/* Allocate the device state FIRST so we have a valid pointer */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	drv_data->usb_dev = usb_dev;
	drv_data->interface = interface;
	drv_data->sequence = 0;
	drv_data->streaming_video = false;
	drv_data->has_stored_header = false;
	drv_data->current_frame_len = 0;
	drv_data->parse_len = 0;

	mutex_init(&drv_data->v4l2_lock);
	spin_lock_init(&drv_data->ready_queue_lock);
	INIT_LIST_HEAD(&drv_data->ready_queue);

	/* 3. Grab and Claim the iAP Interface */
	iap_intf = usb_ifnum_to_if(usb_dev, USEEPLUS_IAP_INTERFACE);
	if (!iap_intf) {
		dev_err(&interface->dev, "Could not find iAP interface\n");
		retval = -ENODEV;
		goto error_free_dev;
	}

	retval = usb_driver_claim_interface(&useeplus_driver, iap_intf, dev);
	if (retval) {
		dev_err(&interface->dev, "Could not claim iAP interface\n");
		goto error_free_dev;
	}

	/* Memory Allocations */
	drv_data->current_frame = vzalloc(MAX_FRAME_SIZE);
	if (!drv_data->current_frame) {
		retval = -ENOMEM;
		goto error_release_iap;
	}

	drv_data->parse_buffer = kzalloc(BULK_TRANSFER_SIZE * 2, GFP_KERNEL);
	if (!drv_data->parse_buffer) {
		retval = -ENOMEM;
		goto error_release_iap;
	}

	/* Dynamically Map Endpoints for VIDEO interface (Must look at Altsetting 1) */
	video_alt = usb_altnum_to_altsetting(interface, USEEPLUS_ALT_SETTING_VIDEO_ENABLE);
	if (!video_alt) {
		dev_err(&interface->dev, "Could not find Video Altsetting\n");
		retval = -ENODEV;
		goto error_release_iap;
	}

	for (i = 0; i < video_alt->desc.bNumEndpoints; ++i) {
		ep_desc = &video_alt->endpoint[i].desc;
		if (usb_endpoint_num(ep_desc) == USEEPLUS_VIDEO_ENDPOINT) {
			if (usb_endpoint_dir_in(ep_desc))
				drv_data->video_in_ep = ep_desc->bEndpointAddress;
			else
				drv_data->video_out_ep = ep_desc->bEndpointAddress;
		}
	}

	/* Dynamically Map Endpoints for iAP interface */
	for (i = 0; i < iap_intf->cur_altsetting->desc.bNumEndpoints; ++i) {
		ep_desc = &iap_intf->cur_altsetting->endpoint[i].desc;
		if (usb_endpoint_num(ep_desc) == USEEPLUS_IAP_ENDPOINT) {
			if (usb_endpoint_dir_in(ep_desc))
				drv_data->iap_in_ep = ep_desc->bEndpointAddress;
			else
				drv_data->iap_out_ep = ep_desc->bEndpointAddress;
		}
	}

	if (!drv_data->video_in_ep || !drv_data->video_out_ep || !drv_data->iap_in_ep || !drv_data->iap_out_ep) {
		dev_err(&interface->dev, "Could not map all 4 required bulk endpoints\n");
		retval = -ENODEV;
		goto error_release_iap;
	}

	/* V4L2 Device Registration */
	drv_data->v4l2_dev.release = useeplus_device_release;
	retval = v4l2_device_register(&interface->dev, &drv_data->v4l2_dev);
	if (retval) {
		dev_err(&interface->dev, "v4l2_device_register failed with error %d\n", retval);
		goto error_release_iap;
	}

	q = &drv_data->video_queue;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_READ;
	q->drv_priv = dev;
	q->buf_struct_size = sizeof(struct useeplus_buffer);
	q->ops = &useeplus_vb2_ops;
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
	drv_data->video_dev.fops = &useeplus_v4l2_fops;
	drv_data->video_dev.ioctl_ops = &useeplus_v4l2_ioctl_ops;
	drv_data->video_dev.release = video_device_release_empty;
	drv_data->video_dev.lock = &drv_data->v4l2_lock;
	drv_data->video_dev.queue = q;
	drv_data->video_dev.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	video_set_drvdata(&drv_data->video_dev, dev);

	/* Hardware Initialization (iAP Drain) */
	iap_heartbeat_sink = kmalloc(HEARTBEAT_SINK_BUFFER_SIZE, GFP_KERNEL);
	if (!iap_heartbeat_sink) {
		retval = -ENOMEM;
		goto error_unreg_v4l2;
	}

	for (i = 0; i < HEARTBEAT_SINK_ITERATIONS; ++i) {
		usb_bulk_msg(
			usb_dev,
			usb_rcvbulkpipe(usb_dev, drv_data->iap_in_ep),
			iap_heartbeat_sink,
			HEARTBEAT_SINK_BUFFER_SIZE,
			&actual_len,
			HEARTBEAT_SINK_TIMEOUT_MS
		);
	}
	kfree(iap_heartbeat_sink);

	retval = usb_set_interface(usb_dev, USEEPLUS_VIDEO_INTERFACE, USEEPLUS_ALT_SETTING_VIDEO_ENABLE);
	if (retval) {
		dev_err(&interface->dev, "usb_set_interface failed with error %d\n", retval);
		goto error_unreg_v4l2;
	}

	retval = usb_clear_halt(usb_dev, usb_rcvbulkpipe(usb_dev, drv_data->video_in_ep));
	if (retval)
		dev_info(&interface->dev, "usb_clear_halt failed with error %d\n", retval);

	retval = useeplus_alloc_urbs(dev);
	if (retval)
		goto error_urbs;

	usb_set_intfdata(interface, dev);

	retval = video_register_device(&drv_data->video_dev, VFL_TYPE_VIDEO, -1);
	if (retval) {
		dev_err(&interface->dev, "video_register_device failed with error %d\n", retval);
		goto error_urbs;
	}

	dev_info(&interface->dev, "Useeplus protocol borescope connected successfully.\n");

	return 0;

/* Teardown: If V4L2 registered, let the release callback free memory. Otherwise, manually clean up. */
error_urbs:
	dev_dbg(&interface->dev, "Rolling back URBs\n");
	useeplus_kill_urbs(dev);
error_unreg_v4l2:
	dev_dbg(&interface->dev, "Unregistering V4L2 device\n");
	v4l2_device_unregister(&drv_data->v4l2_dev);
	return retval;

error_release_iap:
	usb_driver_release_interface(&useeplus_driver, iap_intf);

	if (drv_data->current_frame)
		vfree(drv_data->current_frame);

	kfree(drv_data->parse_buffer);
error_free_dev:
	kfree(dev);
	return retval;
}

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
