// SPDX-License-Identifier: GPL-2.0+ OR MIT

#include "useeplus_protocol.h"

static bool up_check_ghost_header(u8 *buffer, size_t len,
				  size_t current_index, size_t *hdr_off)
{
	size_t limit, o;
	struct up_pkt_hdr *o_pkt;

	if (len - current_index < 3)
		return false;

	limit = len - current_index - 3;
	if (limit > MAX_GHOST_HEADER_OFFSET)
		limit = MAX_GHOST_HEADER_OFFSET;

	for (o = UP_PKT_HDR_SIZE; o <= limit; o++) {
		o_pkt = (struct up_pkt_hdr *)(buffer + current_index + o);

		if (up_is_valid_pkt_header(o_pkt)) {
			*hdr_off = o;
			return true;
		}
	}
	return false;
}

static enum up_decode_status up_decode_envelope(u8 *buffer, size_t len,
						size_t *index_ptr,
						struct up_envelope *env)
{
	struct up_pkt_hdr *pkt_hdr;
	struct up_pl_hdr *pl_hdr;
	size_t hdr_off = 0;
	size_t pl_off;
	u16 pkt_len;
	size_t index = *index_ptr;

	env->index = index;

	pkt_hdr = (struct up_pkt_hdr *)(buffer + index);
	pkt_len = UP_LE16_TO_CPU(pkt_hdr->le_length);

	if (pkt_len > UP_MAX_WIRE_LEN) {
		(*index_ptr)++;
		return UP_PARSE_SKIP;
	}

	env->total_size = UP_PKT_HDR_SIZE + pkt_len;

	if (!up_is_valid_pkt_header(pkt_hdr)) {
		(*index_ptr)++;
		return UP_PARSE_SKIP;
	}

	if (up_check_ghost_header(buffer, len, index, &hdr_off)) {
		*index_ptr += (hdr_off <= (len - index)) ? hdr_off : 1;
		return UP_PARSE_SKIP;
	}

	if (env->total_size > (len - index))
		return UP_PARSE_NEED_DATA;

	if (pkt_len < UP_PL_HDR_SIZE) {
		*index_ptr += env->total_size;
		return UP_PARSE_SKIP;
	}

	pl_off = index + UP_PKT_HDR_SIZE;
	if (pl_off >= len || (len - pl_off) < UP_PL_HDR_SIZE) {
		*index_ptr += env->total_size;
		return UP_PARSE_SKIP;
	}

	pl_hdr = (struct up_pl_hdr *)(buffer + pl_off);

	env->frame_id = pl_hdr->le_frame_id;
	env->cam_num = pl_hdr->le_camera_number;
	env->flags = pl_hdr->le_flags;

	return UP_PARSE_OK;
}

size_t up_decode_bulk(struct up_decoder *decoder, u8 *buffer, size_t len)
{
	size_t i, pl_start, pl_size, emit_size, limit;
	struct up_pl_hdr *pl_hdr;
	struct up_envelope env;
	size_t index = 0;
	u8 *pl_src;
	bool found = false;

	if (len == 0 || !buffer)
		return 0;

	while ((len - index) >= TOTAL_USB_HEADER_SIZE) {
		switch (up_decode_envelope(buffer, len, &index, &env)) {
		case UP_PARSE_NEED_DATA:
			return index;
		case UP_PARSE_SKIP:
			continue;
		case UP_PARSE_OK:
			break;
		}

		if (env.cam_num > MAX_CAM_NUM)
			goto advance_parser;

		if (decoder->building_frame &&
		    decoder->frame_id != env.frame_id) {
			if (!decoder->eof_reached && decoder->cb.on_frame_end)
				decoder->cb.on_frame_end(decoder->context);
		}

		if (!decoder->building_frame ||
		    decoder->frame_id != env.frame_id) {
			if (decoder->cb.on_frame_start)
				decoder->cb.on_frame_start(decoder->context, env.frame_id, env.cam_num);

			decoder->frame_id = env.frame_id;
			decoder->building_frame = true;

			decoder->found_soi = false;
			decoder->eof_reached = false;
		}

		if (decoder->eof_reached)
			goto advance_parser;

		pl_hdr = (struct up_pl_hdr *)(buffer + env.index + UP_PKT_HDR_SIZE);
		if (up_valid_mjpeg_payload(pl_hdr)) {
			pl_start = env.index + TOTAL_USB_HEADER_SIZE;
			pl_size = env.total_size - TOTAL_USB_HEADER_SIZE;
			pl_src = buffer + pl_start;

			if (!decoder->found_soi) {
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
							decoder->found_soi = true;
							found = true;
							break;
						}
					}
				}
				if (!found)
					goto advance_parser;
			}

			emit_size = pl_size;
			if (pl_size >= 2) {
				for (i = 0; i < pl_size - 1; i++) {
					if (pl_src[i] == JPEG_DEL &&
					    pl_src[i + 1] == JPEG_EOI) {
						emit_size = i + 2;
						decoder->eof_reached = true;
						break;
					}
				}
			}

			if (decoder->cb.on_video_payload)
				decoder->cb.on_video_payload(decoder->context, pl_src, emit_size);

			if (decoder->eof_reached && decoder->cb.on_frame_end)
				decoder->cb.on_frame_end(decoder->context);
		}

advance_parser:
		index += env.total_size;
	}

	return index;
}
