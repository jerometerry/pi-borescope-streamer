import sys

def scan_markers(file_path):
    try:
        with open(file_path, 'rb') as f:
            data = f.read()
    except FileNotFoundError:
        print(f"Error: File '{file_path}' not found.")
        sys.exit(1)

    file_len = len(data)
    offset = 0
    marker_count = 0

    print("Stream_Offset_Decimal, Stream_Offset_Hex, Pattern")
    print("-" * 50)

    # Scan byte-by-byte up to the second-to-last byte of the file
    while offset < file_len - 1:
        # Check for the raw sequential byte pair: 0xBB followed immediately by 0xAA
        if data[offset] == 0xBB and data[offset + 1] == 0xAA:
            print(f"{offset}, 0x{offset:X}, 0xBBAA")
            marker_count += 1
            # Step forward by 2 to skip past this confirmed marker
            offset += 2
        else:
            # Shift scanning window by exactly 1 byte to catch misaligned or tightly packed markers
            offset += 1

    print("-" * 50)
    print(f"Scan complete. Found {marker_count} instances of 0xBB 0xAA.")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python scan_sync_markers.py <raw_camera_stream.bin>")
        sys.exit(1)

    scan_markers(sys.argv[1])