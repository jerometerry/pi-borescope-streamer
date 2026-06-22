import sys
import struct

# Protocol and JPEG markers
USB_SYNC_LE = b'\xaa\xbb'  # Little-Endian 0xBBAA
JPEG_SOI    = b'\xff\xd8'  # Start of Image
JPEG_EOI    = b'\xff\xd9'  # End of Image

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

    while offset < file_len - 100:
        current_chunk = data[offset : offset + 2]
        dev_id = data[offset+2]
        le_length = data[offset + 3 : offset + 5]
        le_len_val = struct.unpack_from('<H', data[offset + 3: offset + 5])[0]
        chunk2 = data[offset + 80: offset + 82]

        if current_chunk == USB_SYNC_LE and dev_id == 0x07:
            header_potential_data = data[offset + 5 : offset + 14]
            print(f"--- 0x07 Potential Data [{le_length.hex(' ')}]: [{header_potential_data.hex(' ')}] : [{chunk2.hex(' ')}]")
            # print(f"--- 0x07 Potential Data |{le_length.hex(' ')}|{le_len_val}|: [{header_potential_data.hex(' ')}]")
            offset += 80
            continue

        # No match found, shift scanning window by exactly 1 byte
        offset += 1

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python visualize_markers.py <binary_stream.bin>")
        sys.exit(1)

    visualize_stream(sys.argv[1])
