import struct
import sys
from PIL import Image

# Operational Constants
MAGIC_DELIMITER = 0xAABB  # 0xBBAA in Little-Endian memory representation
USB_HDR_LEN = 5
FRAG_HDR_LEN = 7
TOTAL_HDR_LEN = USB_HDR_LEN + FRAG_HDR_LEN

def parse_and_markup(file_path, output_img_path, width_pixels=1024):
    print(f"Opening binary stream dump: {file_path}")
    with open(file_path, 'rb') as f:
        data = f.read()

    file_len = len(data)
    print(f"Total file size: {file_len} bytes")

    # Create a byte array to classify every single byte's role
    # 0 = Skipped/Garbage, 1 = USB Hdr, 2 = Frag Hdr, 3 = Valid Payload, 4 = Ghost/Corrupt
    classification = bytearray(file_len)

    offset = 0
    valid_frames_count = 0
    ghost_frames_count = 0

    print("\n--- TEXTUAL OFFSET LOG ---")

    while offset <= file_len - USB_HDR_LEN:
        # Scan for the Little-Endian 0xBBAA magic sync marker
        marker = struct.unpack_from('<H', data, offset)[0]

        if marker == MAGIC_DELIMITER:
            # 1. RUN GHOST FRAME FILTER
            # Look ahead up to 160 bytes for a second 0xBBAA marker
            is_ghost = False
            for lookahead in range(1, 161):
                check_offset = offset + lookahead
                if check_offset <= file_len - 2:
                    next_marker = struct.unpack_from('<H', data, check_offset)[0]
                    if next_marker == MAGIC_DELIMITER:
                        is_ghost = True
                        break

            if is_ghost:
                print(f"[!] Ghost Frame Detected at offset {offset} (0x{offset:X}). Marking red and skipping.")
                classification[offset:offset+2] = [4] * 2  # Mark the signature as Ghost
                ghost_frames_count += 1
                offset += 2  # Move past the false marker to continue parsing safely
                continue

            # 2. PROCESS VALID ENVELOPE
            if offset + TOTAL_HDR_LEN > file_len:
                break # File truncated mid-header

            # Parse Envelope details
            _, device_id, le_length = struct.unpack_from('<HBH', data, offset)

            # Parse Fragment details
            frame_id, dev_num, flags, gyro = struct.unpack_from('<BBBI', data, offset + USB_HDR_LEN)

            # Identify packet endpoints based on envelope payload length
            payload_end = offset + USB_HDR_LEN + le_length
            if payload_end > file_len:
                payload_end = file_len # Cap if file truncates prematurely

            print(f"[+] Valid USB Frame | Offset: {offset} (0x{offset:X}) | Length: {le_length} bytes")
            print(f"    [Env]  Device ID: 0x{device_id:02X}")
            print(f"    [Frag] Frame ID: {frame_id} | Device Num: {dev_num} | Flags: 0x{flags:02X} | Gyro: {gyro}")

            # Check for core JPEG signatures inside payload bounds
            payload_start = offset + TOTAL_HDR_LEN
            if payload_start < payload_end:
                # Scan for SOI and EOI safely inside this specific packet window
                packet_payload = data[payload_start:payload_end]
                if b'\xff\xd8' in packet_payload:
                    soi_pos = offset + TOTAL_HDR_LEN + packet_payload.find(b'\xff\xd8')
                    print(f"    --> JPEG SOI Token found at absolute offset: {soi_pos} (0x{soi_pos:X})")
                if b'\xff\xd9' in packet_payload:
                    eoi_pos = offset + TOTAL_HDR_LEN + packet_payload.find(b'\xff\xd9')
                    print(f"    --> JPEG EOI Token found at absolute offset: {eoi_pos} (0x{eoi_pos:X})")

            # Apply classification markers for coloring
            classification[offset : offset + USB_HDR_LEN] = [1] * USB_HDR_LEN
            classification[offset + USB_HDR_LEN : offset + TOTAL_HDR_LEN] = [2] * FRAG_HDR_LEN
            classification[offset + TOTAL_HDR_LEN : payload_end] = [3] * (payload_end - (offset + TOTAL_HDR_LEN))

            valid_frames_count += 1
            offset = payload_end # Advance parsing index cleanly past the processed packet
        else:
            # Not a marker byte, move forward by 1 byte to keep scanning
            offset += 1

    # --- IMAGE RENDER PHASE ---
    print("\nProcessing finished. Synthesizing protocol visualizer grid map...")

    # Calculate dimensions based on requested grid width
    height_pixels = (file_len + width_pixels - 1) // width_pixels

    # Pad classification data array to form a complete rectangle grid
    padded_len = width_pixels * height_pixels
    if len(classification) < padded_len:
        classification.extend([0] * (padded_len - len(classification)))

    # Define color scheme maps
    colors = {
        0: (250, 250, 250),  # White: Uninitialized padding bytes / skipped data
        1: (255, 215, 0),    # Yellow: USB envelope frame header
        2: (60, 179, 113),   # Green: Fragment context metadata block
        3: (30, 144, 255),   # Blue: Extracted video feed payload data
        4: (255, 0, 0)       # Red: Ghost markers/noise skipped via filter
    }

    # Map classifications array into an RGB pixel stream
    pixel_data = [colors[byte_type] for byte_type in classification]

    # Generate Image Canvas via PIL
    img = Image.new('RGB', (width_pixels, height_pixels))
    img.putdata(pixel_data)
    img.save(output_img_path)

    print(f"\n--- REPORT ---")
    print(f"Valid Packets Parsed: {valid_frames_count}")
    print(f"Ghost Signatures Neutralized: {ghost_frames_count}")
    print(f"Visualization Grid Saved Successfully: {output_img_path} ({width_pixels}x{height_pixels})")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python visualize_protocol.py <input_dump.bin> <output_map.png>")
        sys.exit(1)

    parse_and_markup(sys.argv[1], sys.argv[2])
