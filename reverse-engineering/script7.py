import usb.core
import usb.util
import sys
import io
from PIL import Image

def get_true_dimensions(data_bytes):
    """Isolates the raw JPEG payload data slice and reads dimensions safely using PIL."""
    b = bytes(data_bytes)

    # 1. Strip your exact 12-byte custom transport headers
    # up_usb_frm_hdr (5) + up_video_frm_frag_hdr (7) = 12 bytes
    if len(b) <= 12:
        return None
    inner_payload = b[12:]

    # 2. Pinpoint the exact byte location of the true JPEG Start of Image (0xFF, 0xD8)
    soi_index = inner_payload.find(b'\xff\xd8')
    if soi_index == -1:
        return None

    # Slice the data so it begins strictly at the JPEG SOI marker
    jpeg_data = inner_payload[soi_index:]

    # 3. Feed the clean binary slice into the PIL Image Parser engine
    try:
        with Image.open(io.BytesIO(jpeg_data)) as img:
            return img.size # Returns a clean tuple: (Width, Height)
    except Exception as e:
        # If the chunk was too small or truncated, print the error
        return f"Parser error (truncated frame block?): {e}"

# --- Hardware Connection Mapping ---
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

# --- Control Negotiation Pipeline ---
payload = [0] * 26
payload[0] = 0x00
payload[1] = 0x00
payload[2] = 0x02 # MJPEG format index
payload[3] = target_index
interval = 10000000 // 30 # 30 FPS target units
payload[4] = interval & 0xFF
payload[5] = (interval >> 8) & 0xFF
payload[6] = (interval >> 16) & 0xFF
payload[7] = (interval >> 24) & 0xFF

print("--> Sending Setup Control Block Sequence...")
dev.ctrl_transfer(0x21, 0x01, 0x0100, 1, payload)
dev.ctrl_transfer(0x21, 0x01, 0x0200, 1, payload)

# --- Bulk Activation Handshakes ---
print("--> Firing iAP Auth handshakes...")
dev.write(0x02, [0xFF, 0x55, 0xFF, 0x55, 0xEE, 0x10])
dev.write(0x01, [0xBB, 0xAA, 0x05, 0x00, 0x00])

print("--> Capturing raw payload from video endpoint data pipe...")
try:
    # Read a maximum buffer size slice (128KB) to capture an entire full frame
    raw_video_data = dev.read(0x81, 131072, timeout=4000)
    print(f"Captured {len(raw_video_data)} bytes over USB.")

    dimensions = get_true_dimensions(raw_video_data)
    if isinstance(dimensions, tuple):
        print(f"\n[SUCCESS] Unwrapped Stream Resolution for Index {target_index} is: {dimensions[0]} x {dimensions[1]}")
    else:
        print(f"\n[!] Failed to extract geometry profile: {dimensions}")

except Exception as e:
    print(f"Stream Capture Timeout/Error: {e}")
