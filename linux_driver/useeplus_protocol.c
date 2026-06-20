// SPDX-License-Identifier: GPL-2.0+ OR MIT

#include "useeplus_protocol.h"
#include "useeplus_protocol.h"

static bool up_check_ghost_hdr(u8 *buf, size_t len, size_t buf_off, size_t *u_hdr_off)
{
	struct up_usb_frm_hdr *u_hdr;
	size_t o, limit;

	if (UP_USB_FRM_HDR_SIZE + buf_off > len)
		return false;

	limit = len - buf_off - UP_USB_FRM_HDR_SIZE;
	if (limit > MAX_GHOST_HDR_OFF)
		limit = MAX_GHOST_HDR_OFF;

	for (o = UP_USB_FRM_HDR_SIZE; o <= limit; o++) {
		u_hdr = up_get_usb_frm_hdr(buf, buf_off + o);

		if (up_is_valid_usb_frm_hdr(u_hdr)) {
			*u_hdr_off = o;
			return true;
		}
	}

	return false;
}

static enum up_decode_status up_decode(u8 *buf, size_t len, size_t *cur_pos,
				       struct up_decode_state *state)
{
	size_t buf_off, u_hdr_off, v_hdr_off;
	struct up_usb_frm_hdr *u_hdr;
	struct up_video_frm_frag_hdr *v_hdr;
	u16 vff_len;

	u_hdr_off = 0;
	buf_off = *cur_pos;

	if (UP_USB_FRM_HDR_SIZE + buf_off > len)
		return UP_DECODE_NEED_DATA;

	u_hdr = up_get_usb_frm_hdr(buf, buf_off);

	if (!up_is_valid_usb_frm_hdr(u_hdr)) {
		(*cur_pos)++;
		return UP_INVALID_USB_FRM_HDR;
	}

	if (up_check_ghost_hdr(buf, len, buf_off, &u_hdr_off)) {
		/*
		 * Hardware packs 4 944 byte packets into 4K pages, leaving the
		 * remaining 320 bytes uninitialized. This uninitialized data
		 * contains remnants of other packets, which we need to filter out.
		 *
		 * We found another valid packet within a short distance from the
		 * previous one. Treat the short packet as a ghost, and skip it.
		 */
		*cur_pos += u_hdr_off;
		return UP_DECODE_SKIP;
	}

	/*
	 * A valid payload cannot be larger than the packet size (944 bytes),
	 * less the packet header size (5 bytes) and payload header size (7 bytes).
	 * A payload is at most 932 bytes. For our purposes, using an upper bound of
	 * 1024 is quick sanity check against that we aren't looking at uninitialized
	 * data.
	 */
	vff_len = up_get_video_frm_frag_len(u_hdr);
	if (vff_len > UP_MAX_VIDEO_FRM_FRAG_LEN) {
		(*cur_pos)++;
		return UP_INVALID_USB_FRM_HDR;
	}

	state->usb_frm_size = UP_USB_FRM_HDR_SIZE + vff_len;

	if ((state->usb_frm_size + buf_off) > len)
		return UP_DECODE_NEED_DATA;

	if (vff_len < UP_VIDEO_FRM_FRAG_HDR_SIZE) {
		*cur_pos += state->usb_frm_size;
		return UP_DECODE_SKIP;
	}

	v_hdr_off = UP_USB_FRM_HDR_SIZE + buf_off;
	v_hdr = up_get_video_frm_frag_hdr(buf, v_hdr_off);

	state->frame_id = v_hdr->frame_id;
	state->dev_num = v_hdr->device_number;
	state->flags = v_hdr->flags;

	return UP_DECODE_OK;
}

size_t up_decode_bulk(struct up_decoder *dec, u8 *buf, size_t len)
{
	size_t i, video_data_start, video_data_len, img_size, limit, cur_pos;
	struct up_decode_state state;
	struct up_video_frm_frag_hdr *v_hdr;
	u8 *video_data_ptr;
	bool found;

	cur_pos = 0;
	found = false;

	if (len == 0 || !buf)
		return 0;

	while ((len - cur_pos) >= VIDEO_DATA_OFFSET) {
		switch (up_decode(buf, len, &cur_pos, &state)) {
		case UP_DECODE_NEED_DATA:
			return cur_pos;
		case UP_DECODE_SKIP:
			continue;
		case UP_INVALID_USB_FRM_HDR:
			continue;
		case UP_INVALID_VIDEO_FRM_FRAG_HDR:
			continue;
		case UP_IS_GHOST_HDR:
			continue;
		case UP_DECODE_OK:
			break;
		}

		if (state.dev_num > MAX_DEV_NUM)
			goto advance;

		if (dec->building_frame && dec->frame_id != state.frame_id) {
			if (!dec->eof_reached && dec->cb.on_video_frame_incomplete)
				dec->cb.on_video_frame_incomplete(dec->context);
		}

		if (!dec->building_frame || dec->frame_id != state.frame_id) {
			if (dec->cb.on_video_frame_start)
				dec->cb.on_video_frame_start(dec->context,
						       state.frame_id,
						       state.dev_num);

			dec->frame_id = state.frame_id;
			dec->building_frame = true;
			dec->found_soi = false;
			dec->eof_reached = false;
		}

		if (dec->eof_reached)
			goto advance;

		v_hdr = up_get_video_frm_frag_hdr(buf, UP_USB_FRM_HDR_SIZE + cur_pos);

		if (!up_is_valid_video_frm_frag_hdr(v_hdr))
			goto advance;

		video_data_start = VIDEO_DATA_OFFSET + cur_pos;
		video_data_len = state.usb_frm_size - VIDEO_DATA_OFFSET;
		video_data_ptr = buf + video_data_start;

		if (!dec->found_soi) {
			found = false;
			limit = video_data_len;
			if (limit > JPEG_SOI_MAX_POS)
				limit = JPEG_SOI_MAX_POS;

			if (video_data_len >= 2) {
				for (i = 0; i < limit - 1; i++) {
					if (up_is_jpg_soi(video_data_ptr, i)) {
						video_data_ptr += i;
						video_data_len -= i;
						dec->found_soi = true;
						found = true;
						break;
					}
				}
			}

			if (!found)
				goto advance;
		}

		img_size = video_data_len;
		if (video_data_len >= 2) {
			for (i = 0; i < video_data_len - 1; i++) {
				if (up_is_jpg_eoi(video_data_ptr, i)) {
					img_size = i + 2;
					dec->eof_reached = true;
					break;
				}
			}
		}

		if (dec->cb.on_video_frame_fragment)
			dec->cb.on_video_frame_fragment(dec->context, video_data_ptr, img_size);

		if (dec->eof_reached && dec->cb.on_video_frame_complete)
			dec->cb.on_video_frame_complete(dec->context);

advance:
		cur_pos += state.usb_frm_size;
	}

	return cur_pos;
}
