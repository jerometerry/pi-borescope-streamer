try:
    print("Querying current camera layout configuration payload...")
    config_bytes = list(dev.ctrl_transfer(0xA1, 0x81, 0x0100, 1, 26))

    # Let's change the resolution selection parameter (Byte 3)
    # If index 1 is 1080p, index 2 will be the next step down (like 720p or 640x480)
    config_bytes[3] = 2

    print("\nSending modified target resolution parameters to camera...")
    # bmRequestType: 0x21 (Class Interface Out)
    # bRequest:      0x01 (SET_CUR)
    # wValue:        0x0100 (VS_PROBE_CONTROL)
    # wIndex:        1
    dev.ctrl_transfer(0x21, 0x01, 0x0100, 1, config_bytes)

    # Lock the state machine choices into the camera hardware pipeline
    print("Locking in format choice via standard commit phase...")
    dev.ctrl_transfer(0x21, 0x01, 0x0200, 1, config_bytes)
    print("Format configurations committed successfully!")

    print("\nReading back the hardware updated state structure...")
    updated_bytes = dev.ctrl_transfer(0xA1, 0x81, 0x0100, 1, 26)

    # Read bytes 18-21 (dwMaxVideoFrameSize) to see the exact byte layout size change
    frame_size = updated_bytes[18] | (updated_bytes[19] << 8) | (updated_bytes[20] << 16) | (updated_bytes[21] << 24)
    print(f"New Max Video Frame Size (Bytes 18-21): {frame_size} bytes")

except Exception as e:
    print("Transfer execution error:", e)
