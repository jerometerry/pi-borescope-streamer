import usb.core
import usb.util
import sys

def parse_jpeg_dimensions(data_bytes):
    b = list(data_bytes)

    USB_FRM_HDR_LEN = 5
    FRAG_HDR_LEN = 7
    TOTAL_HEADER_LEN = USB_FRM_HDR_LEN + FRAG_HDR_LEN

    if len(b) > TOTAL_HEADER_LEN:
        payload_data = b[TOTAL_HEADER_LEN:]
    else:
        return None

    for i in range(len(payload_data) - 8):
        if payload_data[i] == 0xFF and payload_data[i+1] == 0xC0:
            # Found SOF0 block!
            # Offset i+4: Height (2 bytes, Big Endian)
            # Offset i+6: Width (2 bytes, Big Endian)
            height = (payload_data[i+4] << 8) | payload_data[i+5]
            width = (payload_data[i+6] << 8) | payload_data[i+7]
            return width, height

        if payload_data[i] == 0xFF and payload_data[i+1] == 0xD8:
            print("--> Found JPEG SOI in payload: {i}")

    return None

dev = usb.core.find(idVendor=0x0329, idProduct=0x2022)
if dev is None:
    print("Camera not found!")
    sys.exit(1)

for intf in dev.get_active_configuration():
    if dev.is_kernel_driver_active(intf.bInterfaceNumber):
        dev.detach_kernel_driver(intf.bInterfaceNumber)

dev.set_configuration()
dev.set_interface_altsetting(interface=1, alternate_setting=1)

target_index = int(input("Enter Frame Index to inspect (1, 2, or 3): "))

payload = [0] * 26
payload[0] = 0x00
payload[1] = 0x00
payload[2] = 0x02 # MJPEG Format
payload[3] = target_index
interval = 10000000 // 30 # Target 30 FPS
payload[4] = interval & 0xFF
payload[5] = (interval >> 8) & 0xFF
payload[6] = (interval >> 16) & 0xFF
payload[7] = (interval >> 24) & 0xFF

print("--> Sending Setup Control Block Sequence...")
dev.ctrl_transfer(0x21, 0x01, 0x0100, 1, payload)
dev.ctrl_transfer(0x21, 0x01, 0x0200, 1, payload)

print("--> Firing iAP Auth handshakes...")
dev.write(0x02, [0xFF, 0x55, 0xFF, 0x55, 0xEE, 0x10])
dev.write(0x01, [0xBB, 0xAA, 0x05, 0x00, 0x00])

print("--> Capturing raw payload from video endpoint data pipe...")
try:
    raw_video_data = dev.read(0x81, 131072, timeout=4000)
    print(f"Captured {len(raw_video_data)} bytes over USB connection pipeline.")

    dimensions = parse_jpeg_dimensions(raw_video_data)
    if dimensions:
        print(f"\n[SUCCESS] Unwrapped Stream Resolution for Index {target_index} is: {dimensions[0]} x {dimensions[1]}")
    else:
        print("\n[!] Captured data chunk, but could not find a clear JPEG resolution signature inside.")
        print("First 24 raw stream bytes:", [hex(x) for x in raw_video_data[:24]])

except Exception as e:
    print(f"Stream Capture Timeout/Error: {e}")
