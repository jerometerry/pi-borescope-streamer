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
	size_t buf_len, limit;
	struct up_pkt_hdr *o_pkt;
	u8 *o_hdr_ptr;
	size_t o;

	buf_len = drv_data->decode_buf_len;
	limit = min_t(size_t, MAX_GHOST_HEADER_OFFSET,
		      buf_len - ctx->index - 3);

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

static void up_finalize_active_frame(struct up_drv_data *drv_data)
{
	struct vb2_buffer *vb;
	u8 *vaddr;
	size_t eoi_off;
	bool found_eoi = false;
	int j;

	if (!drv_data->active_buf || drv_data->active_pl_len == 0)
		return;

	vb = &drv_data->active_buf->vb2_buffer.vb2_buf;
	vaddr = vb2_plane_vaddr(vb, 0);
	eoi_off = drv_data->active_pl_len;

	/*
	 * Scan backwards strictly within the bounds of what we wrote for EOI
	 */
	for (j = drv_data->active_pl_len; j >= 2; j--) {
		if (vaddr[j - 2] == JPEG_DEL && vaddr[j - 1] == JPEG_EOI) {
			eoi_off = j;
			found_eoi = true;
			break;
		}
	}

	if (found_eoi) {
		vb2_set_plane_payload(vb, 0, eoi_off);
		vb->timestamp = ktime_get_ns();
		drv_data->active_buf->vb2_buffer.sequence =
			drv_data->sequence++;
		vb2_buffer_done(vb, VB2_BUF_STATE_DONE);
		drv_data->dbg_frames_delivered++;

		/*
		 * Safely release ownership of the buffer back to VB2
		 */
		drv_data->active_buf = NULL;
	} else {
		/*
		 * Recycle the buffer if corrupted/incomplete
		 */
		drv_data->dbg_frames_dropped_eoi++;
	}

	/*
	 * Clear state for the next frame
	 */
	drv_data->active_pl_len = 0;
}

static bool up_trim_preamble(u8 **pl_src, size_t *pl_size)
{
	size_t limit, i;

	/*
	 * Prevent unsigned integer underflow on bounds check
	 */
	if (*pl_size < 2)
		return false;

	limit = min_t(size_t, JPEG_SOI_MAX_POS, *pl_size - 1);

	for (i = 0; i < limit; i++) {
		if ((*pl_src)[i] == JPEG_DEL && (*pl_src)[i + 1] == JPEG_SOI) {
			*pl_src += i;
			*pl_size -= i;
			return true;
		}
	}

	return false;
}

static void up_process_video_payload(struct up_drv_data *drv_data,
				     struct up_parse_ctx *ctx, u8 *pl_src,
				     size_t pl_size)
{
	struct up_buffer *up_buf;
	u8 *vaddr;

	if (pl_size == 0)
		return;

	/*
	 * Grab a new buffer if we don't have one
	 */
	if (!drv_data->active_buf) {
		spin_lock_irqsave(&drv_data->ready_queue_lock, ctx->flags);
		if (!list_empty(&drv_data->ready_queue)) {
			up_buf = list_first_entry(&drv_data->ready_queue,
						  struct up_buffer, list);
			drv_data->active_buf = up_buf;
			list_del(&drv_data->active_buf->list);
		}
		spin_unlock_irqrestore(&drv_data->ready_queue_lock, ctx->flags);
	}

	if (!drv_data->active_buf) {
		/*
		 * Dropping frame, no buffers available
		 */
		return;
	}

	vaddr = vb2_plane_vaddr(&drv_data->active_buf->vb2_buffer.vb2_buf, 0);

	/*
	 * Preamble Trimming: Hunt for SOI on the first payload chunk
	 */
	if (drv_data->active_pl_len == 0) {
		if (!up_trim_preamble(&pl_src, &pl_size)) {
			/*
			 * Drop fragment, wait for SOI
			 */
			return;
		}
	}

	/*
	 * Direct copy to the mapped memory space
	 */
	if (drv_data->active_pl_len + pl_size <= MAX_FRAME_SIZE) {
		memcpy(vaddr + drv_data->active_pl_len, pl_src, pl_size);
		drv_data->active_pl_len += pl_size;
	} else {
		/*
		 * Overflow protection
		 */
		drv_data->active_pl_len = 0;
	}
}

void up_decode_packets(struct up_drv_data *drv_data, struct up_parse_ctx *ctx)
{
	u8 current_frame_id, current_camera_number, current_flags, other_flags;
	size_t pl_start, pl_size, pkt_size, hdr_off, pl_off;
	u8 *hdr_ptr, *pl_src;
	struct up_pl_hdr *payload;
	struct up_pkt_hdr *pkt;
	bool has_gravity_sensor;
	u16 pkt_len;

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

		/*
		 * Frame Boundary Detected
		 */
		if (drv_data->building_frame &&
		    drv_data->frame_id != current_frame_id)
			up_finalize_active_frame(drv_data);

		drv_data->frame_id = current_frame_id;
		drv_data->building_frame = true;
		has_gravity_sensor = (current_flags & 0x01) != 0;
		other_flags = (current_flags >> 2) & 0x3F;

		/*
		 * Process Video Feed Only
		 */
		if (!has_gravity_sensor && other_flags == 0 &&
		    current_camera_number < 2) {
			pl_start = ctx->index + TOTAL_USB_HEADER_SIZE;
			pl_size = pkt_size - TOTAL_USB_HEADER_SIZE;
			pl_src = drv_data->decode_buf + pl_start;

			up_process_video_payload(drv_data, ctx, pl_src,
						 pl_size);
		}

		ctx->index += pkt_size;
	}
}
