import sys
import struct

# Protocol and JPEG markers
USB_SYNC_LE = b'\xaa\xbb'  # Little-Endian 0xBBAA
JPEG_SOI    = b'\xff\xd8'  # Start of Image
JPEG_EOI    = b'\xff\xd9'  # End of Image

def hex_dump_gap(data, gap_start, gap_end):
    gap_data = data[gap_start:gap_end]
    if not gap_data:
        return ""
    return ' '.join(f'{b:02X}' for b in gap_data)

def visualize_stream(file_path):
    try:
        with open(file_path, 'rb') as f:
            data = f.read()
    except FileNotFoundError:
        print(f"Error: File '{file_path}' not found.")
        sys.exit(1)

    file_len = len(data)
    offset = 0

    print(f"{'Offset (Dec)':<15} | {'Offset (Hex)':<15} | {'Marker Tag'}")
    print("-" * 50)

    # Step through the file byte-by-byte for a pure sequential log
    while offset < file_len - 17:
        # Get the next two bytes to check for a 2-byte marker match
        current_chunk = data[offset : offset + 2]
        dev_id = data[offset+2]
        le_len_val = struct.unpack_from('<H', data[offset : offset + 3])[0]
        valid_data_end = offset + 5 + le_len_val
        frm_id = data[offset + 5]
        dev_num = data[offset + 6]
        flags = data[offset + 7]
        gs = data[ offset+ 8 : offset + 12]

        if current_chunk == USB_SYNC_LE:
            # Found 0xBBAA (Stored as AA BB over wire)
            print(f"{offset:<15} | {f'0x{offset:08X}':<15} | [xBBAA SYNC] | dev_id: {dev_id} frm: {frm_id} dev_num: {dev_num} flags: {flags} len: {le_len_val} data_end: {valid_data_end}")
            offset += 2 # Move past the full marker
            continue

        elif current_chunk == JPEG_SOI:
            # Found standard JPEG Start of Image
            print(f"{offset:<15} | {f'0x{offset:08X}':<15} | [JPEG SOI]")
            offset += 2
            continue

        elif current_chunk == JPEG_EOI:
            # Found standard JPEG End of Image
            print(f"{offset:<15} | {f'0x{offset:08X}':<15} | [JPEG EOI]")
            offset += 2
            continue

        # No match found, shift scanning window by exactly 1 byte
        offset += 1

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python visualize_markers.py <binary_stream.bin>")
        sys.exit(1)

    visualize_stream(sys.argv[1])


# Inside your main loop when [xBBAA SYNC]
# On the NEXT iteration (finding the next marker):
# gap_start = valid_data_end_from_previous_loop
# gap_end = current_offset
# print(f"GAP DATA: {hex_dump_gap(data, gap_start, gap_end)}")
