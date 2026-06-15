// SPDX-License-Identifier: GPL-2.0+

#include "include/useeplus.h"
#include <asm/byteorder.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/usb.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>

static bool up_is_valid_header(struct up_pkt_hdr *pkt)
{
	u16 del = le16_to_cpu(pkt->le_delimeter);
	u8 dev_id = pkt->le_device_id;

	return (del == UP_PKT_DEL &&
		(dev_id == VIDEO_CAMERA_ID || dev_id == GRAVITY_SENSOR_ID));
}

static bool up_check_ghost_header(struct up_drv_data *drv_data,
				  struct up_parse_ctx *ctx, size_t *hdr_off)
{
	size_t limit = min_t(size_t, MAX_GHOST_HEADER_OFFSET, buf_len - ctx->index - 3);
	size_t buf_len = drv_data->decode_buf_len;
	struct up_pkt_hdr *o_pkt;
	u8 *o_hdr_ptr;
	size_t o;

	for (o = UP_PKT_HDR_SIZE; o <= limit; o++) {
		o_hdr_ptr = drv_data->decode_buf + ctx->index + o;
		o_pkt = (struct up_pkt_hdr *)o_hdr_ptr;

		if (up_is_valid_header(o_pkt)) {
			*hdr_off = o;
			return true;
		}
	}
	return false;
}

static void up_find_jpeg_boundaries(struct up_drv_data *drv_data,
				    size_t *soi_offset, size_t *eoi_offset,
				    bool *building_frame, bool *found_eoi)
{
	size_t max_pos = min_t(size_t, JPEG_SOI_MAX_POS, drv_data->frame_len);
	size_t f_len = drv_data->frame_len;
	u8 *f_buf = drv_data->frame_buf;
	long j;

	*building_frame = false;
	*found_eoi = false;

	for (j = 0; j + 1 < max_pos; j++) {
		if (f_buf[j] == JPEG_DEL && f_buf[j + 1] == JPEG_SOI) {
			*soi_offset = j;
			*building_frame = true;
			break;
		}
	}

	for (j = f_len; j >= 2; j--) {
		if (f_buf[j - 2] == JPEG_DEL && f_buf[j - 1] == JPEG_EOI) {
			*eoi_offset = j;
			*found_eoi = true;
			break;
		}
	}
}

static void up_deliver_frame_to_client(struct up_drv_data *drv_data,
				       struct up_parse_ctx *ctx)
{
	size_t soi_offset, eoi_offset, img_size;
	bool building_frame, found_eoi;
	struct up_buffer *vbuf;
	void *vaddr;

	drv_data->dbg_frames_found++;
	up_find_jpeg_boundaries(drv_data, &soi_offset, &eoi_offset,
				&building_frame, &found_eoi);

	if (building_frame && found_eoi && soi_offset < eoi_offset) {
		img_size = eoi_offset - soi_offset;
		drv_data->frame_counter++;

		spin_lock_irqsave(&drv_data->ready_queue_lock, ctx->flags);
		if (!list_empty(&drv_data->ready_queue)) {
			vbuf = list_first_entry(&drv_data->ready_queue,
						struct up_buffer, list);
			list_del(&vbuf->list);

			vaddr = vb2_plane_vaddr(&vbuf->vb2_buffer.vb2_buf, 0);
			if (vaddr) {
				memcpy(vaddr, drv_data->frame_buf + soi_offset,
				       img_size);
				vb2_set_plane_payload(&vbuf->vb2_buffer.vb2_buf,
						      0, img_size);

				vbuf->vb2_buffer.vb2_buf.timestamp =
					ktime_get_ns();
				vbuf->vb2_buffer.sequence =
					drv_data->sequence++;
				vbuf->vb2_buffer.field = V4L2_FIELD_NONE;

				vb2_buffer_done(&vbuf->vb2_buffer.vb2_buf,
						VB2_BUF_STATE_DONE);
				drv_data->dbg_frames_delivered++;
			} else {
				vb2_buffer_done(&vbuf->vb2_buffer.vb2_buf,
						VB2_BUF_STATE_ERROR);
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

void up_decode_packets(struct up_drv_data *drv_data, struct up_parse_ctx *ctx)
{
	u8 current_frame_id, current_camera_number, current_flags, other_flags;
	size_t pl_start, pl_size, pkt_size, hdr_off, pl_off;
	struct up_pl_hdr *payload;
	struct up_pkt_hdr *pkt;
	bool has_gravity_sensor;
	u16 pkt_len;
	u8 *hdr_ptr;

	while (ctx->index + TOTAL_USB_HEADER_SIZE <= drv_data->decode_buf_len) {
		hdr_ptr = drv_data->decode_buf + ctx->index;
		pkt = (struct up_pkt_hdr *)(hdr_ptr);
		pkt_len = le16_to_cpu(pkt->le_length);
		pkt_size = UP_PKT_HDR_SIZE + pkt_len;
		hdr_off = 0;

		if (!up_is_valid_header(pkt)) {
			ctx->index++;
			continue;
		}

		if (up_check_ghost_header(drv_data, ctx, &hdr_off)) {
			drv_data->dbg_ghost_headers++;
			ctx->index += hdr_off;
			continue;
		}

		drv_data->dbg_packets_found++;
		if (pkt_len > (URB_SIZE * 2)) {
			dev_dbg(&drv_data->itf->dev,
				"Corrupted pkt_len %u, skipping byte\n",
				pkt_len);
			ctx->index++;
			continue;
		}

		if (ctx->index + pkt_size > drv_data->decode_buf_len)
			break;

		if (pkt_len < UP_PL_HDR_SIZE) {
			ctx->index += pkt_size;
			continue;
		}

		pl_off = ctx->index + UP_PKT_HDR_SIZE;
		payload = (struct up_pl_hdr *)(drv_data->decode_buf + pl_off);
		current_frame_id = payload->le_frame_id;
		current_camera_number = payload->le_camera_number;
		current_flags = payload->le_flags;

		if (drv_data->building_frame && drv_data->frame_len > 0 &&
		    drv_data->frame_id != current_frame_id)
			up_deliver_frame_to_client(drv_data, ctx);

		drv_data->frame_id = current_frame_id;
		drv_data->building_frame = true;
		has_gravity_sensor = (current_flags & 0x01) != 0;
		other_flags = (current_flags >> 2) & 0x3F;

		if (!has_gravity_sensor && other_flags == 0 &&
		    current_camera_number < 2) {
			pl_start = ctx->index + TOTAL_USB_HEADER_SIZE;
			pl_size = pkt_size - TOTAL_USB_HEADER_SIZE;

			if ((drv_data->frame_len + pl_size) <= MAX_FRAME_SIZE) {
				memcpy(drv_data->frame_buf +
					       drv_data->frame_len,
				       drv_data->decode_buf + pl_start,
				       pl_size);
				drv_data->frame_len += pl_size;
			}
		}

		ctx->index += pkt_size;
	}
}
