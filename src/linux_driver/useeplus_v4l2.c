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

#define BULK_TRANSFER_COUNT 4
#define USEEPLUS_IAP_INTERFACE 0
#define USEEPLUS_VIDEO_INTERFACE 1

#define CAP_DRIVER "Useeplus"
#define CAP_CARD "Useeplus non-UVC Borescope"
#define V4L2_INPUT_NAME "Borescope Lens Channel 0"

#define DIAG_DATA_FORMAT "| URBs: %lu | USB Errors: %lu | Packets: %lu " \
			 "| Frames: %lu | Delivered: %lu | Drop SOI: %lu " \
			 "| Drop EOI: %lu | Drop Q: %lu | Ghosts: %lu |\n"

static const unsigned int useeplus_alt_setting_video_enable = 1;

static const int useeplus_video_endpoint = 0x01;
static const int useeplus_iap_endpoint = 0x02;

static const size_t heartbeat_sink_buffer_size = 512;
static const int heartbeat_sink_iterations = 30;
static const int heartbeat_sink_timeout_ms = 100;

static const u16 usb_packet_delimeter = 0xBBAA;
static const u8 video_camera_id = 0x0B;
static const u8 gravity_sensor_id = 0x07;

static const size_t max_ghost_header_offset = 160;
static const size_t jpeg_soi_markers_max_position = 256;

static const u8 jpeg_boundary_marker = 0xFF;
static const u8 jpeg_start_of_img_marker = 0xD8;
static const u8 jpeg_end_of_img_marker = 0xD9;

static const u32 resolution_width = 640;
static const u32 resolution_height = 480;
static const int diagnostic_log_iterations = 300;

static const int usb_timeout_ms = 1000;

static const unsigned int max_frame_size = (256 * 1024);

static const size_t bulk_transfer_size = 16384;

static const u8 iap_auth_handshake[] = { 0xFF, 0x55, 0xFF, 0x55, 0xEE, 0x10 };
static const u8 start_video_command[] = { 0xBB, 0xAA, 0x05, 0x00, 0x00 };

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

static const size_t usb_packet_header_size =
	sizeof(struct usb_packet_header);

static const size_t usb_payload_header_size =
	sizeof(struct usb_payload_header);

static const size_t total_usb_header_size =
	usb_packet_header_size + usb_payload_header_size;

struct useeplus_buffer {
	struct vb2_v4l2_buffer vb2_buffer;
	struct list_head list;
};

enum useeplus_stream_state {
	STREAM_HW_ACTIVE = 0,
	STREAM_CLIENT_READY = 1,
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

static int useeplus_queue_setup(
	struct vb2_queue *vq,
	unsigned int *nbuffers,
	unsigned int *nplanes,
	unsigned int sizes[],
	struct device *alloc_devs[])
{
	if (*nplanes)
		return sizes[0] < max_frame_size ? -EINVAL : 0;

	*nplanes = 1;
	sizes[0] = max_frame_size;
	return 0;
}

static int useeplus_buf_prepare(struct vb2_buffer *vb)
{
	if (vb2_plane_size(vb, 0) < max_frame_size)
		return -EINVAL;

	vb2_set_plane_payload(vb, 0, max_frame_size);
	return 0;
}

static void useeplus_buf_queue(struct vb2_buffer *vb)
{
	struct useeplus_drv_data *drv_data = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *v4l2_buf = to_vb2_v4l2_buffer(vb);

	struct useeplus_buffer *buf =
		container_of(v4l2_buf, struct useeplus_buffer, vb2_buffer);

	unsigned long flags;

	spin_lock_irqsave(&drv_data->ready_queue_lock, flags);
	list_add_tail(&buf->list, &drv_data->ready_queue);
	spin_unlock_irqrestore(&drv_data->ready_queue_lock, flags);
}

static void useeplus_kill_urbs(struct useeplus_drv_data *drv_data)
{
	clear_bit(STREAM_CLIENT_READY, &drv_data->streaming);
	clear_bit(STREAM_HW_ACTIVE, &drv_data->streaming);

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
				usb_free_coherent(
					drv_data->usb_dev,
					bulk_transfer_size,
					drv_data->urb_buffers[i],
					drv_data->urb_dma_addrs[i]
				);
				drv_data->urb_buffers[i] = NULL;
			}
			usb_free_urb(drv_data->urbs[i]);
			drv_data->urbs[i] = NULL;
		}
	}
}

static int useeplus_write_msg(
	struct useeplus_drv_data *drv_data,
	u8 endpoint_addr,
	const u8 *tokens,
	size_t len)
{
	int retval;
	int actual_length;
	u8 *dma_buffer;

	dma_buffer = kmemdup(tokens, len, GFP_KERNEL);

	if (!dma_buffer)
		return -ENOMEM;

	retval = usb_bulk_msg(
		drv_data->usb_dev,
		usb_sndbulkpipe(drv_data->usb_dev, endpoint_addr),
		dma_buffer,
		len,
		&actual_length,
		usb_timeout_ms
	);

	kfree(dma_buffer);
	return retval;
}

static int useeplus_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct useeplus_drv_data *drv_data = vb2_get_drv_priv(vq);
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
	retval = useeplus_write_msg(
		drv_data,
		drv_data->iap_out_ep,
		iap_auth_handshake,
		sizeof(iap_auth_handshake)
	);

	if (retval) {
		dev_err(&drv_data->interface->dev,
			"useeplus_write_msg init failed: %d\n", retval);
		goto error_start;
	}

	retval = useeplus_write_msg(
		drv_data,
		drv_data->video_out_ep,
		start_video_command,
		sizeof(start_video_command)
	);

	if (retval) {
		dev_err(&drv_data->interface->dev,
			"useeplus_write_msg start failed: %d\n", retval);
		goto error_start;
	}

	// Submit URBs to begin pulling the stream
	for (urbs_submitted = 0; urbs_submitted < BULK_TRANSFER_COUNT; ++urbs_submitted) {
		retval = usb_submit_urb(
			drv_data->urbs[urbs_submitted],
			GFP_KERNEL
		);
		if (retval) {
			dev_err(&drv_data->interface->dev,
				"Failed to submit URBs: %d\n", retval);
			goto error_start;
		}
	}

	// Allow URB callback paths to start passing payloads to buffers
	set_bit(STREAM_CLIENT_READY, &drv_data->streaming);

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
		struct useeplus_buffer *buf;

		buf = list_first_entry(
			&drv_data->ready_queue,
			struct useeplus_buffer,
			list
		);
		list_del(&buf->list);

		// Buffers correctly marked as queued for V4L2 cleanup on start error
		vb2_buffer_done(&buf->vb2_buffer.vb2_buf, VB2_BUF_STATE_QUEUED);
	}
	spin_unlock_irqrestore(&drv_data->ready_queue_lock, flags);

	// Clear the HW guard last so a future start_streaming invocation can re-attempt
	clear_bit(STREAM_HW_ACTIVE, &drv_data->streaming);

	return retval;
}

static void useeplus_stop_streaming(struct vb2_queue *vq)
{
	struct useeplus_drv_data *drv_data = vb2_get_drv_priv(vq);
	struct useeplus_buffer *buf;
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
		buf = list_first_entry(
			&drv_data->ready_queue,
			struct useeplus_buffer,
			list
		);

		list_del(&buf->list);

		// Per V4L2 spec, buffers stopped via stop_streaming must be marked as ERROR
		vb2_buffer_done(
			&buf->vb2_buffer.vb2_buf,
			VB2_BUF_STATE_ERROR
		);
	}
	spin_unlock_irqrestore(&drv_data->ready_queue_lock, flags);
}

static const struct vb2_ops useeplus_vb2_ops = {
	.queue_setup = useeplus_queue_setup,
	.buf_prepare = useeplus_buf_prepare,
	.buf_queue = useeplus_buf_queue,
	.start_streaming = useeplus_start_streaming,
	.stop_streaming = useeplus_stop_streaming,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
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
	.owner = THIS_MODULE,
	.open = useeplus_v4l2_open,
	.release = useeplus_v4l2_release,
	.read = vb2_fop_read,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
	.unlocked_ioctl	= video_ioctl2,
};

static int useeplus_vidioc_querycap(
	struct file *file,
	void *priv,
	struct v4l2_capability *cap)
{
	struct useeplus_drv_data *drv_data = video_drvdata(file);

	strscpy(cap->driver, CAP_DRIVER, sizeof(cap->driver));
	strscpy(cap->card, CAP_CARD, sizeof(cap->card));

	usb_make_path(
		drv_data->usb_dev,
		cap->bus_info,
		sizeof(cap->bus_info)
	);

	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE |
		V4L2_CAP_STREAMING |
		V4L2_CAP_DEVICE_CAPS;

	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;

	return 0;
}

static int useeplus_vidioc_fmt_vid_cap(
	struct file *file,
	void *priv,
	struct v4l2_format *f)
{
	f->fmt.pix.width = resolution_width;
	f->fmt.pix.height = resolution_height;
	f->fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
	f->fmt.pix.field = V4L2_FIELD_NONE;
	f->fmt.pix.bytesperline	= 0;
	f->fmt.pix.sizeimage = max_frame_size;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;

	return 0;
}

static int useeplus_vidioc_enum_fmt_vid_cap(
	struct file *file,
	void *priv,
	struct v4l2_fmtdesc *f)
{
	if (f->index > 0)
		return -EINVAL;

	f->pixelformat = V4L2_PIX_FMT_MJPEG;

	return 0;
}

static int useeplus_vidioc_enum_input(
	struct file *file,
	void *priv,
	struct v4l2_input *inp)
{
	if (inp->index > 0)
		return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	strscpy(inp->name, V4L2_INPUT_NAME, sizeof(inp->name));

	return 0;
}

static int useeplus_vidioc_g_input(
	struct file *file,
	void *priv,
	unsigned int *i)
{
	*i = 0;
	return 0;
}

static int useeplus_vidioc_s_input(
	struct file *file,
	void *priv,
	unsigned int i)
{
	return i == 0 ? 0 : -EINVAL;
}

static int useeplus_vidioc_g_parm(
	struct file *file,
	void *priv,
	struct v4l2_streamparm *sp)
{
	if (sp->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	sp->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	sp->parm.capture.timeperframe.numerator = 1;
	sp->parm.capture.timeperframe.denominator = 30;

	return 0;
}

static int useeplus_vidioc_s_parm(
	struct file *file,
	void *priv,
	struct v4l2_streamparm *sp)
{
	return useeplus_vidioc_g_parm(file, priv, sp);
}

static const struct v4l2_ioctl_ops useeplus_v4l2_ioctl_ops = {
	.vidioc_querycap = useeplus_vidioc_querycap,
	.vidioc_g_fmt_vid_cap = useeplus_vidioc_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap = useeplus_vidioc_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = useeplus_vidioc_fmt_vid_cap,
	.vidioc_enum_fmt_vid_cap = useeplus_vidioc_enum_fmt_vid_cap,
	.vidioc_enum_input = useeplus_vidioc_enum_input,
	.vidioc_g_input = useeplus_vidioc_g_input,
	.vidioc_s_input = useeplus_vidioc_s_input,
	.vidioc_g_parm = useeplus_vidioc_g_parm,
	.vidioc_s_parm = useeplus_vidioc_s_parm,
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
};

struct useeplus_parse_ctx {
	size_t index;
	unsigned long flags;
};

static void useeplus_decode_packets(struct useeplus_drv_data *drv_data,
				    struct useeplus_parse_ctx *ctx)
{
	struct useeplus_buffer *vbuf;
	long j;

	while (ctx->index + total_usb_header_size <= drv_data->decode_buf_len) {
		u8 *hdr_ptr = drv_data->decode_buf + ctx->index;
		struct usb_packet_header *pkt = (struct usb_packet_header *)(hdr_ptr);

		uint16_t delimeter = le16_to_cpu(pkt->le_delimeter);
		u8 camera_id = pkt->le_device_id;
		uint16_t packet_len = le16_to_cpu(pkt->le_length);

		// Synchronize stream up to header delimiter matches
		if (delimeter != usb_packet_delimeter ||
		    (camera_id != video_camera_id && camera_id != gravity_sensor_id)) {
			ctx->index++;
			continue;
		}

		// Ghost header filtration validation
		{
			bool is_ghost = false;
			size_t header_offset = 0;
			size_t buf_len = drv_data->decode_buf_len;
			size_t last_index = buf_len - ctx->index - 3;
			size_t ghost_limit = min_t(size_t, max_ghost_header_offset, last_index);
			size_t hdr_sz = usb_packet_header_size;
			size_t offset;

			for (offset = hdr_sz; offset <= ghost_limit; ++offset) {
				u8 *offset_ptr = drv_data->decode_buf;
				u8 *offset_hdr_ptr = offset_ptr + ctx->index + offset;
				struct usb_packet_header *offset_pkt =
					(struct usb_packet_header *)(offset_hdr_ptr);

				uint16_t offset_del = le16_to_cpu(offset_pkt->le_delimeter);
				u8 offset_camera_id = offset_pkt->le_device_id;

				if (offset_del == usb_packet_delimeter &&
				    (offset_camera_id == video_camera_id ||
				     offset_camera_id == gravity_sensor_id)) {
					is_ghost = true;
					header_offset = offset;
					break;
				}
			}

			if (is_ghost) {
				drv_data->dbg_ghost_headers++;
				ctx->index += header_offset;
				continue;
			}
		}

		drv_data->dbg_packets_found++;
		size_t total_packet_size = usb_packet_header_size + packet_len;

		if (total_packet_size > (bulk_transfer_size * 2)) {
			dev_dbg(&drv_data->interface->dev,
				"Corrupted packet_len %u, skipping byte\n", packet_len);
			ctx->index++;
			continue;
		}

		if (ctx->index + total_packet_size > drv_data->decode_buf_len)
			break; // Ran out of data. Wait for next bulk transfer

		if (packet_len < usb_payload_header_size) {
			ctx->index += total_packet_size;
			continue;
		}

		size_t payload_offset = ctx->index + usb_packet_header_size;
		u8 *decoder_ptr = drv_data->decode_buf;
		u8 *payload_ptr = decoder_ptr + payload_offset;
		struct usb_payload_header *payload = (struct usb_payload_header *)(payload_ptr);

		u8 current_frame_id = payload->le_frame_id;
		u8 current_camera_number = payload->le_camera_number;
		u8 current_flags = payload->le_flags;

		// Frame boundary transformation detection
		if (drv_data->building_frame &&
		    drv_data->frame_len > 0 &&
		    drv_data->frame_id != current_frame_id) {

			drv_data->dbg_frames_found++;
			size_t soi_offset = 0;
			size_t eoi_offset = 0;
			bool building_frame = false;
			bool found_eoi = false;
			size_t max_pos = min_t(size_t, jpeg_soi_markers_max_position, drv_data->frame_len);

			// Forward scan for SOI marker
			for (j = 0; j + 1 < max_pos; ++j) {
				if (drv_data->frame_buf[j] == jpeg_boundary_marker &&
				    drv_data->frame_buf[j + 1] == jpeg_start_of_img_marker) {
					soi_offset = j;
					building_frame = true;
					break;
				}
			}

			// Backward scan for EOI marker
			for (j = drv_data->frame_len; j >= 2; --j) {
				if (drv_data->frame_buf[j - 2] == jpeg_boundary_marker &&
				    drv_data->frame_buf[j - 1] == jpeg_end_of_img_marker) {
					eoi_offset = j;
					found_eoi = true;
					break;
				}
			}

			// Deliver extracted JPEG payloads to waiting V4L2 queue slots
			if (building_frame && found_eoi && soi_offset < eoi_offset) {
				size_t final_content_size = eoi_offset - soi_offset;

				drv_data->frame_counter++;

				spin_lock_irqsave(&drv_data->ready_queue_lock, ctx->flags);
				if (!list_empty(&drv_data->ready_queue)) {
					vbuf = list_first_entry(&drv_data->ready_queue,
								struct useeplus_buffer, list);
					list_del(&vbuf->list);

					void *vaddr = vb2_plane_vaddr(&vbuf->vb2_buffer.vb2_buf, 0);

					if (vaddr) {
						memcpy(vaddr, drv_data->frame_buf + soi_offset, final_content_size);
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
				spin_unlock_irqrestore(&drv_data->ready_queue_lock, ctx->flags);
			} else if (!building_frame) {
				drv_data->dbg_frames_dropped_soi++;
			} else if (!found_eoi || eoi_offset <= soi_offset) {
				drv_data->dbg_frames_dropped_eoi++;
			}

			drv_data->frame_len = 0;
		}

		drv_data->frame_id = current_frame_id;
		drv_data->building_frame = true;

		bool has_gravity_sensor = (current_flags & 0x01) != 0;
		uint8_t other_flags = (current_flags >> 2) & 0x3F;

		// Extract frame video components when payload properties align
		if (!has_gravity_sensor && other_flags == 0 && current_camera_number < 2) {
			size_t payload_start = ctx->index + total_usb_header_size;
			size_t payload_size = total_packet_size - total_usb_header_size;
			size_t combined_len = drv_data->frame_len + payload_size;

			if (combined_len <= max_frame_size) {
				u8 *jpg_ptr = drv_data->frame_buf + drv_data->frame_len;
				u8 *decode_ptr = drv_data->decode_buf + payload_start;

				memcpy(jpg_ptr, decode_ptr, payload_size);
				drv_data->frame_len += payload_size;
			}
		}

		ctx->index += total_packet_size;
	}
}

static void useeplus_read_bulk_callback(struct urb *urb)
{
	struct useeplus_drv_data *drv_data = urb->context;
	struct useeplus_parse_ctx ctx = { .index = 0 };
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
	if (drv_data->dbg_urbs_processed % diagnostic_log_iterations == 0) {
		dev_dbg(&drv_data->interface->dev, DIAG_DATA_FORMAT,
			drv_data->dbg_urbs_processed, drv_data->dbg_usb_errors,
			drv_data->dbg_packets_found, drv_data->dbg_frames_found,
			drv_data->dbg_frames_delivered, drv_data->dbg_frames_dropped_soi,
			drv_data->dbg_frames_dropped_eoi, drv_data->dbg_frames_dropped_queue,
			drv_data->dbg_ghost_headers);
	}

	// Append incoming block to decoding workspace
	if (drv_data->decode_buf_len + urb->actual_length <= bulk_transfer_size * 2) {
		memcpy(drv_data->decode_buf + drv_data->decode_buf_len,
		       urb->transfer_buffer, urb->actual_length);
		drv_data->decode_buf_len += urb->actual_length;
	} else {
		dev_warn(&drv_data->interface->dev, "Parse buffer overflow, dropping data\n");
		drv_data->decode_buf_len = 0;
	}

	// Run the decoupled Protocol Decoding Machine
	useeplus_decode_packets(drv_data, &ctx);

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
		if (retval && retval != -ENODEV && retval != -ESHUTDOWN && retval != -ENOENT) {
			dev_err(&drv_data->interface->dev, "usb_submit_urb failed: %d\n", retval);
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
			bulk_transfer_size,
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
			bulk_transfer_size,
			useeplus_read_bulk_callback,
			drv_data
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
	/* Resubmit URBs if 0 is true */
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

	if (drv_data) {
		useeplus_kill_urbs(drv_data);

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

	if (drv_data->frame_buf)
		vfree(drv_data->frame_buf);

	kfree(drv_data->decode_buf);
	kfree(drv_data);
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
	int retval, actual_len;

	// Only bind the driver when the Video Interface is probed
	if (interface->cur_altsetting->desc.bInterfaceNumber != USEEPLUS_VIDEO_INTERFACE)
		return -ENODEV;

	dev_info(&interface->dev, "Useeplus borescope identified\n");

	// Allocate the device state FIRST so we have a valid pointer
	drv_data = kzalloc(sizeof(*drv_data), GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;

	drv_data->usb_dev = usb_dev;
	drv_data->interface = interface;
	drv_data->sequence = 0;
	drv_data->building_frame = false;
	drv_data->frame_len = 0;
	drv_data->decode_buf_len = 0;

	mutex_init(&drv_data->v4l2_lock);
	spin_lock_init(&drv_data->ready_queue_lock);
	INIT_LIST_HEAD(&drv_data->ready_queue);

	// Grab and Claim the iAP Interface
	iap_intf = usb_ifnum_to_if(usb_dev, USEEPLUS_IAP_INTERFACE);

	if (!iap_intf) {
		dev_err(&interface->dev, "Could not find iAP interface\n");
		retval = -ENODEV;
		goto error_free_dev;
	}

	retval = usb_driver_claim_interface(
		&useeplus_driver,
		iap_intf,
		drv_data
	);

	if (retval) {
		dev_err(&interface->dev, "Could not claim iAP interface\n");
		goto error_free_dev;
	}

	/* Memory Allocations */
	drv_data->frame_buf = vzalloc(max_frame_size);
	if (!drv_data->frame_buf) {
		retval = -ENOMEM;
		goto error_release_iap;
	}

	drv_data->decode_buf = kzalloc(bulk_transfer_size * 2, GFP_KERNEL);
	if (!drv_data->decode_buf) {
		retval = -ENOMEM;
		goto error_release_iap;
	}

	// Dynamically Map Endpoints for VIDEO interface (Must look at Altsetting 1)
	video_alt = usb_altnum_to_altsetting(
		interface,
		useeplus_alt_setting_video_enable
	);

	if (!video_alt) {
		dev_err(&interface->dev, "Could not find Video Altsetting\n");
		retval = -ENODEV;
		goto error_release_iap;
	}

	for (int i = 0; i < video_alt->desc.bNumEndpoints; ++i) {
		ep_desc = &video_alt->endpoint[i].desc;
		if (usb_endpoint_num(ep_desc) == useeplus_video_endpoint) {
			if (usb_endpoint_dir_in(ep_desc))
				drv_data->video_in_ep = ep_desc->bEndpointAddress;
			else
				drv_data->video_out_ep = ep_desc->bEndpointAddress;
		}
	}

	// Dynamically Map Endpoints for iAP interface
	for (int i = 0; i < iap_intf->cur_altsetting->desc.bNumEndpoints; ++i) {
		ep_desc = &iap_intf->cur_altsetting->endpoint[i].desc;
		if (usb_endpoint_num(ep_desc) == useeplus_iap_endpoint) {
			if (usb_endpoint_dir_in(ep_desc))
				drv_data->iap_in_ep = ep_desc->bEndpointAddress;
			else
				drv_data->iap_out_ep = ep_desc->bEndpointAddress;
		}
	}

	if (!drv_data->video_in_ep ||
		!drv_data->video_out_ep ||
		!drv_data->iap_in_ep ||
		!drv_data->iap_out_ep) {
		dev_err(&interface->dev, "Could not map all 4 required bulk endpoints\n");
		retval = -ENODEV;
		goto error_release_iap;
	}

	// V4L2 Device Registration
	drv_data->v4l2_dev.release = useeplus_device_release;
	retval = v4l2_device_register(&interface->dev, &drv_data->v4l2_dev);
	if (retval) {
		dev_err(&interface->dev, "v4l2_device_register failed with error %d\n", retval);
		goto error_release_iap;
	}

	q = &drv_data->video_queue;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_READ;
	q->drv_priv = drv_data;
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
	video_set_drvdata(&drv_data->video_dev, drv_data);

	/* Hardware Initialization (iAP Drain) */
	iap_heartbeat_sink = kmalloc(heartbeat_sink_buffer_size, GFP_KERNEL);
	if (!iap_heartbeat_sink) {
		retval = -ENOMEM;
		goto error_unreg_v4l2;
	}

	for (int i = 0; i < heartbeat_sink_iterations; ++i) {
		usb_bulk_msg(
			usb_dev,
			usb_rcvbulkpipe(usb_dev, drv_data->iap_in_ep),
			iap_heartbeat_sink,
			heartbeat_sink_buffer_size,
			&actual_len,
			heartbeat_sink_timeout_ms
		);
	}
	kfree(iap_heartbeat_sink);

	retval = usb_set_interface(usb_dev, USEEPLUS_VIDEO_INTERFACE, useeplus_alt_setting_video_enable);
	if (retval) {
		dev_err(&interface->dev, "usb_set_interface failed with error %d\n", retval);
		goto error_unreg_v4l2;
	}

	retval = usb_clear_halt(usb_dev, usb_rcvbulkpipe(usb_dev, drv_data->video_in_ep));
	if (retval)
		dev_info(&interface->dev, "usb_clear_halt failed with error %d\n", retval);

	retval = useeplus_alloc_urbs(drv_data);
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

/* Teardown: If V4L2 registered, let the release callback free memory. Otherwise, manually clean up. */
error_urbs:
	dev_dbg(&interface->dev, "Rolling back URBs\n");
	useeplus_kill_urbs(drv_data);
error_unreg_v4l2:
	dev_dbg(&interface->dev, "Unregistering V4L2 device\n");
	v4l2_device_unregister(&drv_data->v4l2_dev);
	return retval;

error_release_iap:
	usb_driver_release_interface(&useeplus_driver, iap_intf);

	if (drv_data->frame_buf)
		vfree(drv_data->frame_buf);

	kfree(drv_data->decode_buf);
error_free_dev:
	kfree(drv_data);
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
