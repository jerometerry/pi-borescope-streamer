import sys

def scan_with_device_id(file_path):
    try:
        with open(file_path, 'rb') as f:
            data = f.read()
    except FileNotFoundError:
        print(f"Error: File '{file_path}' not found.")
        sys.exit(1)

    file_len = len(data)
    offset = 0

    bbaa_count = 0
    soi_count = 0
    eoi_count = 0

    print("Stream_Offset_Decimal, Stream_Offset_Hex, Pattern_Found")
    print("-" * 60)

    while offset < file_len - 1:
        byte1 = data[offset]
        byte2 = data[offset + 1]

        # 1. Check for Protocol Delimiter (0xBB 0xAA)
        if byte1 == 0xBB and byte2 == 0xAA:
            # Safely look ahead 1 byte past the 2-byte marker (offset + 2) to read device_id
            if offset + 2 < file_len:
                next_byte = data[offset + 2]
                device_id_str = f"device_id: 0x{next_byte:02X}"
            else:
                device_id_str = "device_id: EOF_TRUNCATED"

            print(f"{offset}, 0x{offset:X}, 0xBBAA ({device_id_str})")
            bbaa_count += 1
            offset += 2  # Advance past the 2-byte token
            continue

        # 2. Check for JPEG Start of Image (0xFF 0xD8)
        if byte1 == 0xFF and byte2 == 0xD8:
            print(f"{offset}, 0x{offset:X}, JPEG_SOI")
            soi_count += 1
            offset += 2
            continue

        # 3. Check for JPEG End of Image (0xFF 0xD9)
        if byte1 == 0xFF and byte2 == 0xD9:
            print(f"{offset}, 0x{offset:X}, JPEG_EOI")
            eoi_count += 1
            offset += 2
            continue

        # Shift scanning window forward by exactly 1 byte
        offset += 1

    print("-" * 60)
    print("SCAN SUMMARY:")
    print(f"  Total 0xBBAA Delimiters Found : {bbaa_count}")
    print(f"  Total JPEG SOI Markers Found  : {soi_count}")
    print(f"  Total JPEG EOI Markers Found  : {eoi_count}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python scan_with_device_id.py <raw_camera_stream.bin>")
        sys.exit(1)

    scan_with_device_id(sys.argv[1])
