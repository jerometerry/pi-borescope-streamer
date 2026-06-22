# protocol_gap_visualizer.py
import sys
import struct

# Constants from your driver
USB_SYNC_LE = b'\xaa\xbb'
USB_HDR_SIZE = 5

def visualize_gaps(file_path):
    with open(file_path, 'rb') as f:
        data = f.read()

    offset = 0
    last_end = 0

    while offset < len(data) - 17:
        if data[offset:offset+2] == USB_SYNC_LE:
            # 1. Identify the Gap before this header
            if last_end != 0 and offset > last_end:
                gap_size = offset - last_end
                gap_bytes = data[last_end:offset]
                # Only log if the gap isn't just zeros
                hex_view = gap_bytes[:16].hex(' ') + ("..." if gap_size > 16 else "")
                print(f"--- GAP at {last_end:<10} | Size: {gap_size:<4} | Data: {hex_view}")

            # 2. Parse current Header
            _, dev_id, le_len = struct.unpack_from('<HBH', data, offset)

            # 3. Log the Valid Packet
            print(f"PACKET at {offset:<7} | ID: {dev_id:<2} | Len: {le_len:<4} | Next expected: {offset + 5 + le_len}")

            # Update tracking
            last_end = offset + 5 + le_len
            offset += 2 # Move past marker
            continue

        offset += 1

if __name__ == "__main__":
    visualize_gaps(sys.argv[1])
