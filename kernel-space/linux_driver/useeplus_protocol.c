// SPDX-License-Identifier: GPL-2.0+ OR MIT

#include "useeplus_protocol.h"
#include "include/useeplus_protocol.h"

static bool up_check_ghost_hdr(u8 *buf, size_t len, size_t index, size_t *hdr_off)
{
	struct up_pkt_hdr *o_pkt;
	size_t o, limit;

	if (index + UP_PKT_HDR_SIZE > len)
		return false;

	limit = len - index - UP_PKT_HDR_SIZE;
	if (limit > MAX_GHOST_HDR_OFF)
		limit = MAX_GHOST_HDR_OFF;

	for (o = UP_PKT_HDR_SIZE; o <= limit; o++) {
		o_pkt = up_get_pkt_hdr(buf, index + o);

		if (up_is_valid_pkt_hdr(o_pkt)) {
			*hdr_off = o;
			return true;
		}
	}

	return false;
}

static enum up_decode_status up_decode(u8 *buf, size_t len, size_t *index_ptr,
				       struct up_decode_state *state)
{
	size_t hdr_off, pl_off, index;
	struct up_pkt_hdr *pkt_hdr;
	struct up_pl_hdr *pl_hdr;
	u16 pl_len;

	hdr_off = 0;
	index = *index_ptr;

	if (index + UP_PKT_HDR_SIZE > len)
		return UP_DECODE_NEED_DATA;

	pkt_hdr = up_get_pkt_hdr(buf, index);

	if (!up_is_valid_pkt_hdr(pkt_hdr)) {
		(*index_ptr)++;
		return UP_DECODE_INVALID_PKT;
	}

	if (up_check_ghost_hdr(buf, len, index, &hdr_off)) {
		/*
		 * Hardware packs 4 944 byte packets into 4K pages, leaving the
		 * remaining 320 bytes uninitialized. This uninitialized data
		 * contains remnants of other packets, which we need to filter out.
		 *
		 * We found another valid packet within a short distance from the
		 * previous one. Treat the short packet as a ghost, and skip it.
		 */
		*index_ptr += hdr_off;
		return UP_DECODE_SKIP;
	}

	/*
	 * A valid payload cannot be larger than the packet size (944 bytes),
	 * less the packet header size (5 bytes) and payload header size (7 bytes).
	 * A payload is at most 932 bytes. For our purposes, using an upper bound of
	 * 1024 is quick sanity check against that we aren't looking at uninitialized
	 * data.
	 */
	pl_len = up_get_pl_len(pkt_hdr);
	if (pl_len > UP_MAX_WIRE_LEN) {
		(*index_ptr)++;
		return UP_DECODE_INVALID_PKT;
	}

	state->pkt_size = UP_PKT_HDR_SIZE + pl_len;

	if ((index + state->pkt_size) > len)
		return UP_DECODE_NEED_DATA;

	if (pl_len < UP_PL_HDR_SIZE) {
		*index_ptr += state->pkt_size;
		return UP_DECODE_SKIP;
	}

	pl_off = index + UP_PKT_HDR_SIZE;
	pl_hdr = up_get_pl_hdr(buf, pl_off);

	state->frame_id = pl_hdr->le_frame_id;
	state->dev_num = pl_hdr->le_device_number;
	state->flags = pl_hdr->le_flags;

	return UP_DECODE_OK;
}

size_t up_decode_bulk(struct up_decoder *dec, u8 *buf, size_t len)
{
	size_t i, pl_start, pl_size, emit_size, limit, index;
	struct up_decode_state state;
	struct up_pl_hdr *pl_hdr;
	u8 *pl_src;
	bool found;

	index = 0;
	found = false;

	if (len == 0 || !buf)
		return 0;

	while ((len - index) >= TOTAL_USB_HDR_SIZE) {
		switch (up_decode(buf, len, &index, &state)) {
		case UP_DECODE_NEED_DATA:
			return index;
		case UP_DECODE_SKIP:
			continue;
		case UP_DECODE_INVALID_PKT:
			continue;
		case UP_DECODE_OK:
			break;
		}

		if (state.dev_num > MAX_DEV_NUM)
			goto advance;

		if (dec->building_frame && dec->frame_id != state.frame_id) {
			if (!dec->eof_reached && dec->cb.on_frame_incomplete)
				dec->cb.on_frame_incomplete(dec->context);
		}

		if (!dec->building_frame || dec->frame_id != state.frame_id) {
			if (dec->cb.on_frame_start)
				dec->cb.on_frame_start(dec->context,
						       state.frame_id,
						       state.dev_num);

			dec->frame_id = state.frame_id;
			dec->building_frame = true;
			dec->found_soi = false;
			dec->eof_reached = false;
		}

		if (dec->eof_reached)
			goto advance;

		pl_hdr = up_get_pl_hdr(buf, index + UP_PKT_HDR_SIZE);

		if (!up_valid_mjpeg_pl(pl_hdr))
			goto advance;

		pl_start = index + TOTAL_USB_HDR_SIZE;
		pl_size = state.pkt_size - TOTAL_USB_HDR_SIZE;
		pl_src = buf + pl_start;

		if (!dec->found_soi) {
			found = false;
			limit = pl_size;
			if (limit > JPEG_SOI_MAX_POS)
				limit = JPEG_SOI_MAX_POS;

			if (pl_size >= 2) {
				for (i = 0; i < limit - 1; i++) {
					if (pl_src[i] == JPEG_DEL &&
						pl_src[i + 1] == JPEG_SOI) {
						pl_src += i;
						pl_size -= i;
						dec->found_soi = true;
						found = true;
						break;
					}
				}
			}
			if (!found)
				goto advance;
		}

		emit_size = pl_size;
		if (pl_size >= 2) {
			for (i = 0; i < pl_size - 1; i++) {
				if (pl_src[i] == JPEG_DEL &&
					pl_src[i + 1] == JPEG_EOI) {
					emit_size = i + 2;
					dec->eof_reached = true;
					break;
				}
			}
		}

		if (dec->cb.on_video_payload)
			dec->cb.on_video_payload(dec->context, pl_src, emit_size);

		if (dec->eof_reached && dec->cb.on_frame_complete)
			dec->cb.on_frame_complete(dec->context);

advance:
		index += state.pkt_size;
	}

	return index;
}
