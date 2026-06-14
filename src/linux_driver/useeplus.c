// SPDX-License-Identifier: GPL-2.0+

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/usb.h>
#include <asm/byteorder.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>

#include "useeplus.h"

static bool useeplus_is_valid_header(struct usb_packet_header *pkt)
{
	uint16_t delimiter = le16_to_cpu(pkt->le_delimeter);
	u8 camera_id = pkt->le_device_id;

	return (delimiter == USB_PACKET_DELIMITER &&
		(camera_id == VIDEO_CAMERA_ID || camera_id == GRAVITY_SENSOR_ID));
}

static bool useeplus_check_ghost_header(struct useeplus_drv_data *drv_data,
					struct useeplus_parse_ctx *ctx,
					size_t *header_offset)
{
	size_t buf_len = drv_data->decode_buf_len;
	size_t last_index = buf_len - ctx->index - 3;
	size_t ghost_limit = min_t(size_t, MAX_GHOST_HEADER_OFFSET, last_index);
	size_t hdr_sz = USB_PACKET_HEADER_SIZE;
	size_t offset;

	for (offset = hdr_sz; offset <= ghost_limit; ++offset) {
		u8 *offset_hdr_ptr = drv_data->decode_buf + ctx->index + offset;
		struct usb_packet_header *offset_pkt = (struct usb_packet_header *)offset_hdr_ptr;

		if (useeplus_is_valid_header(offset_pkt)) {
			*header_offset = offset;
			return true;
		}
	}
	return false;
}

static void useeplus_find_jpeg_boundaries(struct useeplus_drv_data *drv_data,
					  size_t *soi_offset, size_t *eoi_offset,
					  bool *building_frame, bool *found_eoi)
{
	long j;
	size_t max_pos = min_t(size_t, JPEG_SOI_MARKERS_MAX_POSITION, drv_data->frame_len);

	*building_frame = false;
	*found_eoi = false;

	for (j = 0; j + 1 < max_pos; ++j) {
		if (drv_data->frame_buf[j] == JPEG_BOUNDARY_MARKER &&
		    drv_data->frame_buf[j + 1] == JPEG_START_OF_IMG_MARKER) {
			*soi_offset = j;
			*building_frame = true;
			break;
		}
	}

	for (j = drv_data->frame_len; j >= 2; --j) {
		if (drv_data->frame_buf[j - 2] == JPEG_BOUNDARY_MARKER &&
		    drv_data->frame_buf[j - 1] == JPEG_END_OF_IMG_MARKER) {
			*eoi_offset = j;
			*found_eoi = true;
			break;
		}
	}
}

static void useeplus_deliver_frame_to_client(struct useeplus_drv_data *drv_data,
					     struct useeplus_parse_ctx *ctx)
{
	struct useeplus_buffer *vbuf;
	size_t soi_offset = 0;
	size_t eoi_offset = 0;
	bool building_frame;
	bool found_eoi;

	drv_data->dbg_frames_found++;

	useeplus_find_jpeg_boundaries(drv_data, &soi_offset, &eoi_offset, &building_frame, &found_eoi);

	if (building_frame && found_eoi && soi_offset < eoi_offset) {
		size_t final_content_size = eoi_offset - soi_offset;

		drv_data->frame_counter++;

		spin_lock_irqsave(&drv_data->ready_queue_lock, ctx->flags);
		if (!list_empty(&drv_data->ready_queue)) {
			vbuf = list_first_entry(&drv_data->ready_queue, struct useeplus_buffer, list);
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

void useeplus_decode_packets(struct useeplus_drv_data *drv_data,
			     struct useeplus_parse_ctx *ctx)
{
	while (ctx->index + TOTAL_USB_HEADER_SIZE <= drv_data->decode_buf_len) {
		u8 *hdr_ptr = drv_data->decode_buf + ctx->index;
		struct usb_packet_header *pkt = (struct usb_packet_header *)(hdr_ptr);
		uint16_t packet_len = le16_to_cpu(pkt->le_length);
		size_t total_packet_size = USB_PACKET_HEADER_SIZE + packet_len;
		size_t header_offset = 0;

		if (!useeplus_is_valid_header(pkt)) {
			ctx->index++;
			continue;
		}

		if (useeplus_check_ghost_header(drv_data, ctx, &header_offset)) {
			drv_data->dbg_ghost_headers++;
			ctx->index += header_offset;
			continue;
		}

		drv_data->dbg_packets_found++;

		if (total_packet_size > (BULK_TRANSFER_SIZE * 2)) {
			dev_dbg(&drv_data->interface->dev,
				"Corrupted packet_len %u, skipping byte\n", packet_len);
			ctx->index++;
			continue;
		}

		if (ctx->index + total_packet_size > drv_data->decode_buf_len)
			break;

		if (packet_len < USB_PAYLOAD_HEADER_SIZE) {
			ctx->index += total_packet_size;
			continue;
		}

		size_t payload_offset = ctx->index + USB_PACKET_HEADER_SIZE;
		struct usb_payload_header *payload =
			(struct usb_payload_header *)(drv_data->decode_buf + payload_offset);

		u8 current_frame_id = payload->le_frame_id;
		u8 current_camera_number = payload->le_camera_number;
		u8 current_flags = payload->le_flags;

		if (drv_data->building_frame &&
		    drv_data->frame_len > 0 &&
		    drv_data->frame_id != current_frame_id) {
			useeplus_deliver_frame_to_client(drv_data, ctx);
		}

		drv_data->frame_id = current_frame_id;
		drv_data->building_frame = true;

		bool has_gravity_sensor = (current_flags & 0x01) != 0;
		uint8_t other_flags = (current_flags >> 2) & 0x3F;

		if (!has_gravity_sensor && other_flags == 0 && current_camera_number < 2) {
			size_t payload_start = ctx->index + TOTAL_USB_HEADER_SIZE;
			size_t payload_size = total_packet_size - TOTAL_USB_HEADER_SIZE;
			size_t combined_len = drv_data->frame_len + payload_size;

			if (combined_len <= MAX_FRAME_SIZE) {
				u8 *jpg_ptr = drv_data->frame_buf + drv_data->frame_len;
				u8 *decode_ptr = drv_data->decode_buf + payload_start;

				memcpy(jpg_ptr, decode_ptr, payload_size);
				drv_data->frame_len += payload_size;
			}
		}

		ctx->index += total_packet_size;
	}
}
