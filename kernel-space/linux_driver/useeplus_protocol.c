// SPDX-License-Identifier: GPL-2.0+

#include "useeplus_protocol.h"

static bool up_check_ghost_header(const u8 *buffer, size_t len, size_t index,
				  size_t *hdr_off)
{
	struct up_pkt_hdr *pkt_hdr;
	size_t limit;
	size_t o;

	limit = (len - index - 3 < 160) ? len - index - 3 : 160;

	for (o = UP_PKT_HDR_SIZE; o <= limit; o++) {
		pkt_hdr = (struct up_pkt_hdr *)(buffer + index + o);
		if (up_is_valid_pkt_header(pkt_hdr)) {
			*hdr_off = o;
			return true;
		}
	}
	return false;
}

size_t up_parser_feed(struct up_parser *parser, const u8 *buffer, size_t len)
{
	size_t index, pkt_len, total_size, ghost_off, pl_size;
	struct up_pkt_hdr *pkt_hdr;
	struct up_pl_hdr *pl_hdr;
	u8 *pl_src;

	while (index + TOTAL_USB_HEADER_SIZE <= len) {
		pkt_hdr = (struct up_pkt_hdr *)(buffer + index);
		pkt_len = UP_LE16_TO_CPU(pkt_hdr->le_length);
		total_size = UP_PKT_HDR_SIZE + pkt_len;
		ghost_off = 0;

		if (!up_is_valid_pkt_header(pkt_hdr)) {
			index++;
			continue;
		}

		if (up_check_ghost_header(buffer, len, index, &ghost_off)) {
			index += (ghost_off <= (len - index)) ? ghost_off : 1;
			continue;
		}

		if (total_size > (len - index))
			break;

		if (pkt_len < UP_PL_HDR_SIZE) {
			index += total_size;
			continue;
		}

		pl_hdr = (struct up_pl_hdr *)(buffer + index + UP_PKT_HDR_SIZE);

		if (!up_valid_mjpeg_payload(pl_hdr)) {
			index += total_size;
			continue;
		}

		if (parser->building_frame && parser->frame_id != pl_hdr->le_frame_id) {
			if (parser->cb.on_frame_end)
				parser->cb.on_frame_end(parser->ctx);
		}

		if (!parser->building_frame || parser->frame_id != pl_hdr->le_frame_id) {
			if (parser->cb.on_frame_start)
				parser->cb.on_frame_start(parser->ctx,
							  pl_hdr->le_frame_id,
							  pl_hdr->le_camera_number);
			parser->frame_id = pl_hdr->le_frame_id;
			parser->building_frame = true;
		}

		if (parser->cb.on_video_payload) {
			pl_src = buffer + index + TOTAL_USB_HEADER_SIZE;
			pl_size = total_size - TOTAL_USB_HEADER_SIZE;
			parser->cb.on_video_payload(parser->ctx, pl_src, pl_size);
		}

		index += total_size;
	}
	return index;
}
