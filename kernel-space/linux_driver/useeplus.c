// SPDX-License-Identifier: GPL-2.0+

#include "useeplus.h"
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
	size_t current_len;
	u8 *vaddr;

	if (unlikely(pl_size == 0 || !pl_src))
		return;

	/*
	 * Secure a fresh buffer if needed
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

	/*
	 * Sanitize and snapshot the state variable to protect against EMI mid-run
	 */
	current_len = drv_data->active_pl_len;
	if (unlikely(current_len >= MAX_FRAME_SIZE))
		goto overflow_reset;

	vaddr = vb2_plane_vaddr(&drv_data->active_buf->vb2_buffer.vb2_buf, 0);
	if (unlikely(!vaddr)) {
		drv_data->active_buf = NULL;
		return;
	}

	/*
	 * Preamble Trimming on first payload chunk
	 */
	if (current_len == 0) {
		if (!up_trim_preamble(&pl_src, &pl_size)) {
			/*
			 * Drop fragment, wait for SOI
			 */
			return;
		}
	}

	/*
	 * Overflow-Safe Math Check
	 */
	if (likely(pl_size <= (MAX_FRAME_SIZE - current_len))) {
		/*
		 * Secondary mid-execution invariant guard for pointer math
		 */
		if (unlikely((current_len + pl_size) > MAX_FRAME_SIZE))
			goto overflow_reset;

		memcpy(vaddr + current_len, pl_src, pl_size);
		drv_data->active_pl_len = current_len + pl_size;
	} else {
		goto overflow_reset;
	}

	return;

overflow_reset:
	/*
	 * Hard, fail-secure rollback of the driver state
	 */
	dev_err_ratelimited(&drv_data->itf->dev,
			    "useeplus: Overflow Prevention. Size: %zu, Current: %zu\n",
			    pl_size, current_len);

	drv_data->active_pl_len = 0;
	drv_data->building_frame = false;
	drv_data->dbg_frames_dropped_soi++;

	/*
	 * Recycle or decouple the corrupted buffer state safely
	 */
	drv_data->active_buf = NULL;
}

void up_decode_packets(struct up_drv_data *drv_data, struct up_parse_ctx *ctx)
{
	size_t pl_start, pl_size, pkt_size, hdr_off, pl_off, cur_index, max_buf_len;
	u8 cur_frm_id, cur_cam_num, cur_flags, other_flags, *hdr_ptr, *pl_src;
	struct up_pl_hdr *payload;
	struct up_pkt_hdr *pkt;
	bool has_gravity_sensor;
	u16 pkt_len;

	max_buf_len = drv_data->decode_buf_len;
	if (unlikely(max_buf_len == 0 || !drv_data->decode_buf))
		return;

	while (ctx->index <= max_buf_len &&
	       (max_buf_len - ctx->index) >= TOTAL_USB_HEADER_SIZE) {
		cur_index = ctx->index;
		hdr_ptr = drv_data->decode_buf + cur_index;
		pkt = (struct up_pkt_hdr *)(hdr_ptr);
		pkt_len = le16_to_cpu(pkt->le_length);

		/*
		 * Checks against the actual absolute physical wire capacity of the __le16 field.
		 */
		if (unlikely(pkt_len > UP_MAX_WIRE_LEN)) {
			ctx->index++;
			continue;
		}

		/*
		 * Explicitly verify that UP_PKT_HDR_SIZE + pkt_len won't overflow size_t
		 */
		if (unlikely(pkt_len > (SIZE_MAX - UP_PKT_HDR_SIZE))) {
			ctx->index++;
			continue;
		}
		pkt_size = UP_PKT_HDR_SIZE + pkt_len;
		hdr_off = 0;

		if (!up_is_valid_header(pkt)) {
			ctx->index++;
			continue;
		}

		if (up_check_ghost_header(drv_data, ctx, &hdr_off)) {
			drv_data->dbg_ghost_headers++;
			if (likely(hdr_off <= (max_buf_len - cur_index)))
				ctx->index += hdr_off;
			else
				ctx->index++;

			continue;
		}

		drv_data->dbg_packets_found++;

		/*
		 * Check that total calculated packet size doesn't overrun our buffer window
		 */
		if (pkt_size > (max_buf_len - cur_index))
			break;

		/*
		 * A packet payload must at least be large enough to contain the 7-byte header
		 */
		if (pkt_len < UP_PL_HDR_SIZE) {
			ctx->index += pkt_size;
			continue;
		}

		pl_off = cur_index + UP_PKT_HDR_SIZE;

		/*
		 * Verify pl_off structure alignment fits within safe boundaries
		 */
		if (unlikely(pl_off >= max_buf_len ||
			     (max_buf_len - pl_off) <
				     sizeof(struct up_pl_hdr))) {
			ctx->index += pkt_size;
			continue;
		}

		payload = (struct up_pl_hdr *)(drv_data->decode_buf + pl_off);

		cur_frm_id = payload->le_frame_id;
		cur_cam_num = payload->le_camera_number;
		cur_flags = payload->le_flags;

		/*
		 * Frame Boundary Tracking
		 */
		if (drv_data->building_frame &&
		    drv_data->frame_id != cur_frm_id)
			up_finalize_active_frame(drv_data);

		drv_data->frame_id = cur_frm_id;
		drv_data->building_frame = true;
		has_gravity_sensor = (cur_flags & 0x01) != 0;
		other_flags = (cur_flags >> 2) & 0x3F;

		/*
		 * Process Video Feed Only (Camera Stream ID 0x0B checked via up_is_valid_header)
		 */
		if (!has_gravity_sensor && other_flags == 0 &&
		    cur_cam_num < 2) {
			if (likely(pkt_size >= TOTAL_USB_HEADER_SIZE)) {
				pl_start = cur_index + TOTAL_USB_HEADER_SIZE;
				pl_size = pkt_size - TOTAL_USB_HEADER_SIZE;

				if (likely(pl_start < max_buf_len &&
					   pl_size <=
						   (max_buf_len - pl_start))) {
					pl_src =
						drv_data->decode_buf + pl_start;
					up_process_video_payload(
						drv_data, ctx, pl_src, pl_size);
				}
			}
		}

		/*
		 * Hard infinite loop defense against EMI register corruption
		 */
		if (unlikely(pkt_size == 0 ||
			     pkt_size > (max_buf_len - cur_index)))
			ctx->index++;
		else
			ctx->index += pkt_size;
	}
}
