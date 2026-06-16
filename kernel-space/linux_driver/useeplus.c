// SPDX-License-Identifier: GPL-2.0+

#include "useeplus.h"
#include <asm/byteorder.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/usb.h>
#include <linux/build_bug.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>

static_assert(UP_MAX_WIRE_LEN <= U16_MAX, "UP_MAX_WIRE_LEN exceeds absolute u16 bounds");

static inline struct up_pkt_hdr *up_get_pkt_hdr(struct up_drv_data *drv_data,
						size_t index)
{
	return (struct up_pkt_hdr *)(drv_data->decode_buf + index);
}

static inline struct up_pl_hdr *up_get_pl_hdr(struct up_drv_data *drv_data,
					      size_t index)
{
	return (struct up_pl_hdr *)(drv_data->decode_buf + index + UP_PKT_HDR_SIZE);
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

		if (up_is_valid_pkt_header(o_pkt)) {
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
		/*
		 * If we didn't get a virtual memory address to the video plane, recycle
		 * drv_data->active_buf to use on the next attempt.
		 */
		drv_data->active_pl_len = 0;
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

	/*
	 * We didn't successfully deliver a frame. Recycle drv_data->active_buf to use on the
	 * next attempt.
	 */
	drv_data->active_pl_len = 0;
	drv_data->building_frame = false;
	drv_data->dbg_frames_dropped_soi++;
}

static inline void up_extract_video_packet(struct up_drv_data *drv_data,
					   struct up_parse_ctx *ctx,
					   size_t cur_index, size_t pkt_size)
{
	size_t pl_start, pl_size, dec_buf_len;
	u8 *pl_src;

	dec_buf_len = drv_data->decode_buf_len;

	if (unlikely(pkt_size < TOTAL_USB_HEADER_SIZE))
		return;

	pl_start = cur_index + TOTAL_USB_HEADER_SIZE;
	pl_size = pkt_size - TOTAL_USB_HEADER_SIZE;

	if (unlikely(pl_start >= dec_buf_len || pl_size > (dec_buf_len - pl_start)))
		return;

	pl_src = drv_data->decode_buf + pl_start;
	up_process_video_payload(drv_data, ctx, pl_src, pl_size);
}

static enum up_parse_status up_parse_envelope(struct up_drv_data *drv_data,
					      struct up_parse_ctx *ctx,
					      struct up_envelope *env)
{
	size_t dec_buf_len = drv_data->decode_buf_len;
	struct up_pkt_hdr *pkt_hdr;
	struct up_pl_hdr *pl_hdr;
	size_t hdr_off = 0;
	size_t pl_off;
	u16 pkt_len;

	env->index = ctx->index;

	pkt_hdr = up_get_pkt_hdr(drv_data, env->index);
	pkt_len = le16_to_cpu(pkt_hdr->le_length);

	if (unlikely(pkt_len > UP_MAX_WIRE_LEN)) {
		ctx->index++;
		return UP_PARSE_SKIP;
	}

	env->total_size = UP_PKT_HDR_SIZE + pkt_len;

	if (!up_is_valid_pkt_header(pkt_hdr)) {
		ctx->index++;
		return UP_PARSE_SKIP;
	}

	if (up_check_ghost_header(drv_data, ctx, &hdr_off)) {
		drv_data->dbg_ghost_headers++;
		ctx->index += likely(hdr_off <= (dec_buf_len - env->index)) ? hdr_off : 1;
		return UP_PARSE_SKIP;
	}

	drv_data->dbg_packets_found++;

	if (env->total_size > (dec_buf_len - env->index))
		return UP_PARSE_NEED_DATA;

	if (pkt_len < UP_PL_HDR_SIZE) {
		ctx->index += env->total_size;
		return UP_PARSE_SKIP;
	}

	pl_off = env->index + UP_PKT_HDR_SIZE;
	if (unlikely(pl_off >= dec_buf_len || (dec_buf_len - pl_off) < UP_PL_HDR_SIZE)) {
		ctx->index += env->total_size;
		return UP_PARSE_SKIP;
	}

	pl_hdr = up_get_pl_hdr(drv_data, env->index);

	env->frame_id = pl_hdr->le_frame_id;
	env->cam_num = pl_hdr->le_camera_number;
	env->flags = pl_hdr->le_flags;

	return UP_PARSE_OK;
}

static inline bool up_can_proceed(struct up_drv_data *drv_data,
				  struct up_parse_ctx *ctx)
{
	size_t dec_buf_len = drv_data->decode_buf_len;

	if (unlikely(ctx->index > dec_buf_len))
		return false;

	return (dec_buf_len - ctx->index) >= TOTAL_USB_HEADER_SIZE;
}

void up_decode_packets(struct up_drv_data *drv_data, struct up_parse_ctx *ctx)
{
	size_t dec_buf_len = drv_data->decode_buf_len;
	struct up_envelope env;

	if (unlikely(dec_buf_len == 0 || !drv_data->decode_buf))
		return;

	while (up_can_proceed(drv_data, ctx)) {
		switch (up_parse_envelope(drv_data, ctx, &env)) {
		case UP_PARSE_NEED_DATA:
			return;
		case UP_PARSE_SKIP:
			continue;
		case UP_PARSE_OK:
			break;
		}

		if (unlikely(env.cam_num > MAX_CAM_NUM))
			goto advance_parser;

		if (drv_data->building_frame && drv_data->frame_id != env.frame_id)
			up_finalize_active_frame(drv_data);

		drv_data->frame_id = env.frame_id;
		drv_data->building_frame = true;

		if (up_has_gravity_sensor(env.flags) || up_has_other_flags(env.flags))
			goto advance_parser;

		up_extract_video_packet(drv_data, ctx, env.index, env.total_size);

advance_parser:
		ctx->index += env.total_size;
	}
}
