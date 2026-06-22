# Save as production_extractor.py
import struct
import sys

def extract_clean_stream(file_path):
    with open(file_path, 'rb') as f:
        data = f.read()

    file_len = len(data)
    offset = 0

    jpeg_buffer = bytearray()
    frame_counter = 0
    gyro_packets = 0

    print("Processing stream based on verified Little-Endian 1024-byte slot grid...")
    print("-" * 80)

    while offset < file_len - 12:
        # Check for our verified Little-Endian 0xBBAA delimiter (0xAA 0xBB in bytes)
        if data[offset] == 0xAA and data[offset+1] == 0xBB:

            # Unpack the 5-byte up_usb_frm_hdr
            # <H = u16 delimiter, B = u8 device_id, H = u16 le_length
            _, device_id, le_length = struct.unpack_from('<HBH', data, offset)

            # Filter for true architectural frames
            if device_id in [0x0B, 0x07]:

                # Unpack the 7-byte up_video_frm_frag_hdr
                # B = frame_id, B = device_number, B = flags, I = le_gravity_sensor
                frame_id, dev_num, flags, gyro = struct.unpack_from('<BBBI', data, offset + 5)

                # Extract button press state from the second bit of the flags field
                button_pressed = bool(flags & 0x02)

                if device_id == 0x07:
                    # Process Telemetry Frame (80 bytes)
                    gyro_packets += 1
                    # print(f"[{offset}] Gyro Telemetry | Value: {gyro} | Button: {button_pressed}")
                    offset += 1024 # Advance to the next strict hardware slot boundary
                    continue

                if device_id == 0x0B:
                    # Process Video Frame Fragment (944 bytes)
                    # The actual payload starts after our 12 bytes of packed headers
                    payload_start = offset + 12
                    payload_end = offset + 5 + le_length

                    if payload_end > file_len:
                        break

                    fragment_payload = data[payload_start:payload_end]

                    # Look for standard JPEG boundary tokens inside our safe payload slice
                    if b'\xff\xd8' in fragment_payload:
                        # Start of a brand new JPEG frame! Clear the accumulation buffer
                        jpeg_buffer = bytearray()
                        soi_idx = fragment_payload.find(b'\xff\xd8')
                        jpeg_buffer.extend(fragment_payload[soi_idx:])
                    elif len(jpeg_buffer) > 0:
                        # Middle or end fragment, append the raw payload data
                        jpeg_buffer.extend(fragment_payload)

                    if b'\xff\xd9' in fragment_payload:
                        # End of Image reached! Save the completed frame to disk
                        eoi_idx = jpeg_buffer.find(b'\xff\xd9')
                        final_jpg = jpeg_buffer[:eoi_idx + 2]

                        output_name = f"extracted_frame_{frame_counter:03d}.jpg"
                        with open(output_name, "wb") as img_out:
                            img_out.write(final_jpg)

                        print(f"[SUCCESS] Extracted Frame {frame_counter} | Size: {len(final_jpg)} bytes -> Saved as {output_name}")
                        frame_counter += 1
                        jpeg_buffer = bytearray() # Reset tracking engine

                    offset += 1024 # Safely skip past the uninitialized slot tail to the next block
                    continue

        # If we hit an unaligned gap, slip forward byte-by-byte to catch the next anchor lock
        offset += 1

    print("-" * 80)
    print(f"Extraction Summary:")
    print(f"  Successfully Rendered JPEG Images : {frame_counter}")
    print(f"  Processed Gyroscopic Telemetry Packets : {gyro_packets}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python production_extractor.py <raw_camera_stream.bin>")
        sys.exit(1)
    extract_clean_stream(sys.argv[1])
