import struct
import sys

# Operational Constants
MAGIC_DELIMITER = 0xAABB  # 0xBBAA in Little-Endian memory representation
USB_HDR_LEN = 5
FRAG_HDR_LEN = 7
TOTAL_HDR_LEN = USB_HDR_LEN + FRAG_HDR_LEN

def parse_and_log(file_path):
    try:
        with open(file_path, 'rb') as f:
            data = f.read()
    except FileNotFoundError:
        print(f"Error: File '{file_path}' not found.")
        sys.exit(1)

    file_len = len(data)
    offset = 0

    while offset <= file_len - USB_HDR_LEN:
        # Scan for the Little-Endian 0xBBAA magic sync marker
        marker = struct.unpack_from('<H', data, offset)

        if marker[0] == MAGIC_DELIMITER:
            # Ghost Frame Filter: Look ahead up to 160 bytes for a second 0xBBAA marker
            is_ghost = False
            for lookahead in range(1, 161):
                check_offset = offset + lookahead
                if check_offset <= file_len - 2:
                    next_marker = struct.unpack_from('<H', data, check_offset)
                    if next_marker[0] == MAGIC_DELIMITER:
                        is_ghost = True
                        break

            if is_ghost:
                # Ghost packet neutralized; step past this false marker
                offset += 2
                continue

            # Ensure there is enough data remaining for a complete 12-byte header set
            if offset + TOTAL_HDR_LEN > file_len:
                break

            # Parse 5-byte Envelope header details
            _, device_id, le_length = struct.unpack_from('<HBH', data, offset)

            # Parse 7-byte Fragment details
            frame_id, dev_num, flags, gyro = struct.unpack_from('<BBBI', data, offset + USB_HDR_LEN)

            # Bound the payload according to the reported inner envelope size
            payload_end = offset + USB_HDR_LEN + le_length
            if payload_end > file_len:
                payload_end = file_len

            # Default tracking values for JPEG bounds inside this frame slice
            j_soi_out = -1
            j_eoi_out = -1

            payload_start = offset + TOTAL_HDR_LEN
            if payload_start < payload_end:
                packet_payload = data[payload_start:payload_end]

                # Check for JPEG Start of Image (SOI) token
                soi_idx = packet_payload.find(b'\xff\xd8')
                if soi_idx != -1:
                    j_soi_out = payload_start + soi_idx

                # Check for JPEG End of Image (EOI) token
                eoi_idx = packet_payload.find(b'\xff\xd9')
                if eoi_idx != -1:
                    j_eoi_out = payload_start + eoi_idx

            # Build the exact line output format
            # [Stream Offset], [Start of Frame Delimiter], [Device ID], [Length],
            # [video frame id], [device num], [flags], [gravity sensor], [JPEG SOI offset], [JPEG EOI offset]
            print(
                f"{offset}, 0xBBAA, 0x{device_id:02X}, {le_length}, "
                f"{frame_id}, {dev_num}, 0x{flags:02X}, {gyro}, "
                f"{j_soi_out if j_soi_out != -1 else 'None'}, "
                f"{j_eoi_out if j_eoi_out != -1 else 'None'}"
            )

            # Proceed cleanly past the length of the processed packet structure
            offset = payload_end
        else:
            # Shift scanning window forward by 1 byte to keep checking
            offset += 1

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python parse_protocol.py <raw_stream_dump.bin>")
        sys.exit(1)

    parse_and_log(sys.argv[1])
