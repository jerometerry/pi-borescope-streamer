import usb.core
import usb.util
import sys

def parse_jpeg_dimensions(data_bytes):
    b = list(data_bytes)
    for i in range(len(b) - 8):
        if b[i] == 0xFF and b[i+1] == 0xC0:
            height = (b[i+4] << 8) | b[i+5]
            width = (b[i+6] << 8) | b[i+7]
            return width, height
    return None

# 1. Connect to the camera
dev = usb.core.find(idVendor=0x0329, idProduct=0x2022)
if dev is None:
    print("Camera not found!")
    sys.exit(1)

# Clean up kernel space drivers
for intf in dev.get_active_configuration():
    if dev.is_kernel_driver_active(intf.bInterfaceNumber):
        dev.detach_kernel_driver(intf.bInterfaceNumber)

dev.set_configuration()

# --- THE FIX: Explicitly activate Interface 1, Alternate Setting 1 ---
print("--> Activating Video Interface Alternate Setting 1...")
dev.set_interface_altsetting(interface=1, alternate_setting=1)

# Ask which index you want to inspect
target_index = int(input("Enter Frame Index to inspect (1, 2, or 3): "))

# 2. Negotiate parameters via the Control Pipe
payload = [0] * 26
payload[0] = 0x00
payload[1] = 0x00
payload[2] = 0x02 # MJPEG format
payload[3] = target_index
interval = 10000000 // 30 # Default 30 FPS
payload[4] = interval & 0xFF
payload[5] = (interval >> 8) & 0xFF
payload[6] = (interval >> 16) & 0xFF
payload[7] = (interval >> 24) & 0xFF

print("--> Initializing Setup Control Sequence...")
dev.ctrl_transfer(0x21, 0x01, 0x0100, 1, payload)
dev.ctrl_transfer(0x21, 0x01, 0x0200, 1, payload)

# 3. Fire Handshake Sequences
print("--> Firing iAP Auth Sequence...")
iap_auth_handshake = [0xFF, 0x55, 0xFF, 0x55, 0xEE, 0x10]
dev.write(0x02, iap_auth_handshake) # EP 2 OUT (Interface 0)

print("--> Sending Start Video Stream Token...")
start_video_command = [0xBB, 0xAA, 0x05, 0x00, 0x00]
dev.write(0x01, start_video_command) # EP 1 OUT (Interface 1 - Now active!)

# 4. Grab raw streaming video payload from EP 1 IN
print("--> Listening to data pipe (EP 1 IN) to capture a video frame packet...")
try:
    # Read a generous chunk of streaming data (e.g., 64KB)
    raw_video_data = dev.read(0x81, 65536, timeout=4000)
    print(f"Captured {len(raw_video_data)} bytes of streaming video.")

    # Extract JPEG dimensions
    dimensions = parse_jpeg_dimensions(raw_video_data)
    if dimensions:
        print(f"\nSUCCESS! True Stream Resolution for Index {target_index} is: {dimensions[0]} x {dimensions[1]}")
    else:
        print("\nCaptured data chunk successfully, but could not find a clean JPEG resolution header token.")
        print("First 16 bytes of data:", [hex(x) for x in raw_video_data[:16]])

except Exception as e:
    print(f"Stream Capture Timeout/Error: {e}")
