#ifndef USEEPLUS_PROTOCOL_H
#define USEEPLUS_PROTOCOL_H

// NOLINTBEGIN(bugprone-reserved-identifier,cppcoreguidelines-use-enum-class,modernize-use-using,cppcoreguidelines-macro-usage)

#ifdef __KERNEL__
/* --- 1. LINUX KERNEL TREE BUILD --- */
#include <linux/types.h>
#include <asm/byteorder.h>

#define UP_LE16_TO_CPU(x) le16_to_cpu(x)
#define UP_LE32_TO_CPU(x) le32_to_cpu(x)

#else
/* --- USER-SPACE BUILDS (Linux Off-Tree & macOS) --- */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
#else
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#endif

#ifndef __packed
#define __packed __attribute__((packed))
#endif

#if defined(__APPLE__)
/* --- 2. MACOS BUILD (Clang) --- */
#include <libkern/OSByteOrder.h>
#ifdef __cplusplus
inline uint16_t UP_LE16_TO_CPU(uint16_t x) { return OSSwapLittleToHostInt16(x); }
inline uint32_t UP_LE32_TO_CPU(uint32_t x) { return OSSwapLittleToHostInt32(x); }
#else
#define UP_LE16_TO_CPU(x) OSSwapLittleToHostInt16(x)
#define UP_LE32_TO_CPU(x) OSSwapLittleToHostInt32(x)
#endif

#else
/* --- 3. LINUX USER-SPACE BUILD (GCC / Clang) --- */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include <endian.h>
#ifdef __cplusplus
inline uint16_t UP_LE16_TO_CPU(uint16_t x) { return le16toh(x); }
inline uint32_t UP_LE32_TO_CPU(uint32_t x) { return le32toh(x); }
#else
#define UP_LE16_TO_CPU(x) le16toh(x)
#define UP_LE32_TO_CPU(x) le32toh(x)
#endif
#endif

#endif /* __KERNEL__ */

/*
 * Useeplus USB Packet Structure (Applies to ALL packets)
 *
 * | Byte Offset | Field Name       | Size | Description                         |
 * |-------------|------------------|------|-------------------------------------|
 * | 0x00        | Packet Delimiter | 2    | 0xBBAA (Little-Endian)              |
 * | 0x02        | Device ID        | 1    | 0x0B = Video, 0x07 = Gravity Sensor |
 * | 0x03        | Payload Length   | 2    | Total bytes following Packet Header |
 * | 0x05        | Frame ID         | 1    | Rolls over when a new frame starts  |
 * | 0x06        | Device Number    | 1    | Secondary internal lens index       |
 * | 0x07        | Flags            | 1    | Bit 0: Gravity, Bit 1: Button       |
 * | 0x08        | IMU Matrix       | 4    | 32-bit accelerometer telemetry      |
 * | 0x0C        | Video Payload    | Var  | Fragmented chunk of MJPEG stream    |
 *
 * Video Payload Stream Rules
 *
 * - Start of Frame: The Video Payload of the first packet for a given Frame ID
 * will begin with the JPEG SOI Marker (FF D8), usually followed by the
 * APP0/JFIF headers.
 * - Continuation: Subsequent packets for the same Frame ID will contain raw
 * JPEG stream data starting immediately at Byte 0x0C.
 * - End of Frame: The final packet for a given Frame ID will contain the JPEG
 * EOI Marker (FF D9) somewhere within its Video Payload block. Uninitialized
 * padding bytes may exist between the EOI marker and the declared Payload
 * Length.
 *
 * Memory Alignment and Uninitialized Memory
 *
 * 4KB Page Alignment
 *
 * The hardware's internal DMA (Direct Memory Access) buffers are aligned into
 * 4-Kilobyte (4096 bytes) pages. A standard Useeplus video packet is exactly
 * 944 bytes long (12 bytes of transport header + 932 bytes of payload).
 *
 * The hardware aggressively packs exactly four full packets into a single 4KB
 * page: 4 packets * 944 bytes = 3776 bytes.
 *
 * This packing leaves exactly 320 bytes of unused space at the tail end of
 * every 4KB page (4096 - 3776 = 320).
 *
 * Uninitialized Memory
 *
 * The hardware does not zero out or initialize these 320 bytes before
 * transmitting the USB buffer. The data in the unused memory is arbitrary,
 * often containing valid packet headers from previous or newer packets. There
 * are no checksums built into the protocol for error detection, which poses a
 * challenge when decoding the video stream.
 *
 * 1. Signature Check
 *
 * Every packet evaluation begins by ensuring the current pointer sits exactly
 * on the 0xBBAA delimiter and a valid Device ID (0x0B or 0x07). If this
 * signature fails, the parser enters Seek Mode (see Step 4).
 *
 * 2. Ghost Header Look-Ahead
 *
 * Before the decoder ever reads or trusts the le_length field of a newly
 * discovered signature, it performs a bounded look-ahead. It scans the next
 * 160 bytes of memory.
 *
 * - If another perfect 0xBBAA signature is found within a short distance, it
 * proves the hardware stuttered or the current header is a ghost remnant.
 * - The decoder treats the current header as a ghost, advances the pointer to
 * the newly discovered real header, skipping the "garbage data".
 *
 * 3. Length Validation
 *
 * If no ghost header is found, the decoder reads le_length and sanity-checks it
 * against an upper bound of UP_MAX_WIRE_LEN (1024 bytes).
 *
 * - If the length exceeds 1024, it means the decoder is looking at garbage data
 * that happens to start with 0xBBAA. The decoder rejects the packet and
 * enters Seek Mode.
 *
 * 4. Seek Mode
 *
 * Whenever the signature fails, or a massive garbage length is detected, the
 * decoder returns an INVALID_PKT state.
 *
 * - Continue the decoding loop, moving ahead by 1 byte. The decoder will
 * continue incrementally until a valid 0xBBAA hardware signature is found, or
 * until it exhausts the data arriving from the FIFO work queue.
 */

struct up_decoder_callbacks {
	void (*on_frame_start)(void *context, u8 frame_id, u8 dev_num);
	void (*on_video_payload)(void *context, u8 *data, size_t len);
	void (*on_frame_complete)(void *context);
	void (*on_frame_incomplete)(void *context);
};

struct up_decoder {
	struct up_decoder_callbacks cb;
	void *context;

	int frame_id;
	bool building_frame;
	bool found_soi;
	bool eof_reached;
};

/* Safe C++ binding wrapper */
#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
constexpr int UP_MAX_WIRE_LEN = 1024;
constexpr int JPEG_SOI_MAX_POS = 256;
constexpr int MAX_GHOST_HDR_OFF = 160;

// Ensure your header values match what constants.hpp expects:
constexpr size_t UP_PKT_HDR_SIZE = 8;  // Update this to your protocol's actual size
constexpr size_t UP_PL_HDR_SIZE = 4;   // Update this to your protocol's actual size
constexpr size_t TOTAL_USB_HDR_SIZE = UP_PKT_HDR_SIZE + UP_PL_HDR_SIZE;
#else
#define UP_MAX_WIRE_LEN 1024
#define JPEG_SOI_MAX_POS 256
#define MAX_GHOST_HDR_OFF 160

#define UP_PKT_HDR_SIZE 8
#define UP_PL_HDR_SIZE 4
#define TOTAL_USB_HDR_SIZE (UP_PKT_HDR_SIZE + UP_PL_HDR_SIZE)
#endif

#ifdef __cplusplus
enum up_usb_topology : std::uint8_t {
#else
enum up_usb_topology {
#endif
	UP_IAP_INTERFACE = 0,
	UP_VIDEO_INTERFACE = 1,
	UP_ALT_VIDEO_ENABLE = 1,
	UP_VIDEO_ENDPOINT = 0x01,
	UP_IAP_ENDPOINT = 0x02,
};

#ifdef __cplusplus
enum up_hw_signatures : std::uint16_t {
#else
enum up_hw_signatures {
#endif
	UP_PKT_DEL = 0xBBAA,
	VIDEO_CAMERA_ID = 0x0B,
	GRAVITY_SENSOR_ID = 0x07,
	MAX_DEV_NUM = 1,
};

#ifdef __cplusplus
enum up_jpeg_marker : std::uint8_t {
#else
enum up_jpeg_marker {
#endif
	JPEG_DEL = 0xFF,
	JPEG_SOI = 0xD8,
	JPEG_EOI = 0xD9,
};

#ifdef __cplusplus
enum up_decode_status : std::uint8_t {
#else
enum up_decode_status {
#endif
	UP_DECODE_OK,
	UP_DECODE_INVALID_PKT,
	UP_DECODE_SKIP,
	UP_DECODE_NEED_DATA,
};

size_t up_decode_bulk(struct up_decoder *decoder, u8 *buffer, size_t len);

#ifdef __cplusplus
}
#endif

struct up_pkt_hdr {
	u16 le_delimiter;
	u8 le_device_id;
	u16 le_length;
} __packed;

struct up_pl_hdr {
	u8 le_frame_id;
	u8 le_device_number;
	u8 le_flags;
	u32 le_gravity_sensor;
} __packed;

struct up_decode_context {
	size_t index;
	unsigned long flags;

	u8 *vaddr;

	struct up_buffer *active_buf;
	size_t active_pl_len;

	u8 *decode_buf;
	size_t decode_buf_len;
};

struct up_decode_state {
	size_t pkt_size;
	u8 frame_id;
	u8 dev_num;
	u8 flags;
};

static inline bool up_is_valid_dev_id(u8 dev_id)
{
	return (dev_id == VIDEO_CAMERA_ID || dev_id == GRAVITY_SENSOR_ID);
}

static inline bool up_is_valid_pkt_del(u16 delimiter)
{
	return (delimiter == UP_PKT_DEL);
}

static inline u16 up_get_pkt_del(struct up_pkt_hdr *pkt)
{
	return UP_LE16_TO_CPU(pkt->le_delimiter);
}

static inline u16 up_get_pl_len(struct up_pkt_hdr *pkt)
{
	return UP_LE16_TO_CPU(pkt->le_length);
}

static inline bool up_check_pkt_hdr(u16 del, u8 dev_id)
{
	return (up_is_valid_pkt_del(del) && up_is_valid_dev_id(dev_id));
}

static inline struct up_pkt_hdr *up_get_pkt_hdr(u8 *buffer, size_t index)
{
	return (struct up_pkt_hdr *)(buffer + index);
}

static inline struct up_pl_hdr *up_get_pl_hdr(u8 *buffer, size_t index)
{
	return (struct up_pl_hdr *)(buffer + index);
}

static inline bool up_is_valid_pkt_hdr(struct up_pkt_hdr *pkt)
{
	u16 del = up_get_pkt_del(pkt);
	u8 dev_id = pkt->le_device_id;

	return up_check_pkt_hdr(del, dev_id);
}

static inline bool up_is_jpg_soi(u8 *pl, size_t i)
{
	return (pl[i] == JPEG_DEL && pl[i + 1] == JPEG_SOI);
}

static inline bool up_is_jpg_eoi(u8 *pl, size_t i)
{
	return (pl[i] == JPEG_DEL && pl[i + 1] == JPEG_EOI);
}

static inline bool up_has_gravity_sensor(u8 flags)
{
	return (flags & 0x01) != 0;
}

static inline bool up_is_button_pressed(u8 flags)
{
	return (flags & 0x02) != 0;
}

static inline u8 up_get_other_flags(u8 flags)
{
	return ((flags >> 2) & 0x3F);
}

static inline bool up_has_other_flags(u8 flags)
{
	return up_get_other_flags(flags) != 0;
}

static inline void up_set_has_gravity_sensor(struct up_pl_hdr *pl, bool has_gs)
{
	uint8_t val = pl->le_flags;

	if (has_gs)
		val |= 0x01;
	else
		val &= ~0x01;

	pl->le_flags = val;
}

static inline void up_set_button_pressed(struct up_pl_hdr *pl, bool pressed)
{
	uint8_t val = pl->le_flags;

	if (pressed)
		val |= 0x02;
	else
		val &= ~0x02;

	pl->le_flags = val;
}

static inline void up_set_other_flags(struct up_pl_hdr *pl, uint8_t other)
{
	uint8_t val = pl->le_flags;

	val &= 0x03;
	val |= ((other & 0x3F) << 2);
	pl->le_flags = val;
}

static inline bool up_valid_mjpeg_pl(struct up_pl_hdr *pl)
{
	if (!pl)
		return false;
	if (pl->le_device_number > MAX_DEV_NUM)
		return false;
	if (up_has_gravity_sensor(pl->le_flags))
		return false;
	if (up_has_other_flags(pl->le_flags))
		return false;
	return true;
}

// NOLINTEND(bugprone-reserved-identifier,cppcoreguidelines-use-enum-class,modernize-use-using,cppcoreguidelines-macro-usage)

#endif /* USEEPLUS_PROTOCOL_H */
