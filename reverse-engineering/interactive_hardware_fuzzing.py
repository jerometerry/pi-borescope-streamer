import usb.core
import usb.util
import sys

def calculate_interval(fps):
    """Convert standard FPS integers into 100ns UVC timing intervals."""
    if fps <= 0:
        return 333333 # Fallback default for 30 FPS
    return 10000000 // fps

def parse_updated_payload(payload):
    """Break down the 26-byte camera feedback buffer into readable metrics."""
    if len(payload) < 26:
        return "Invalid payload length received."

    fmt_idx = payload[2]
    frame_idx = payload[3]

    # Read dwFrameInterval (Bytes 4-7, Little Endian)
    interval = payload[4] | (payload[5] << 8) | (payload[6] << 16) | (payload[7] << 24)
    calculated_fps = 10000000 // interval if interval > 0 else 0

    # Read dwMaxVideoFrameSize (Bytes 18-21, Little Endian)
    max_frame_size = payload[18] | (payload[19] << 8) | (payload[20] << 16) | (payload[21] << 24)

    output = (
        f"\n--- CAMERA FEEDBACK REPORT ---\n"
        f"Format Index (Byte 2)        : {fmt_idx} (Expected 2 for MJPEG)\n"
        f"Frame/Resolution Index (Byte 3): {frame_idx}\n"
        f"Raw Frame Interval (Bytes 4-7) : {interval} units\n"
        f"Calculated Speed               : {calculated_fps} FPS\n"
        f"Max Video Frame Size (18-21)   : {max_frame_size} bytes\n"
    )
    return output

# 1. Connect to the camera
dev = usb.core.find(idVendor=0x0329, idProduct=0x2022)
if dev is None:
    print("Error: Geek szitman supercamera not found!")
    sys.exit(1)

# 2. Release any active system drivers
for intf in dev.get_active_configuration():
    if dev.is_kernel_driver_active(intf.bInterfaceNumber):
        try:
            dev.detach_kernel_driver(intf.bInterfaceNumber)
            print(f"Detached kernel driver on interface {intf.bInterfaceNumber}")
        except Exception as e:
            print(f"Failed to detach interface {intf.bInterfaceNumber}: {e}")

dev.set_configuration()

print("\n=== INTERACTIVE CAMERA EXPERIMENT TOOL ===")
print("You can test frame rates and resolution index numbers here.")
print("Type 'exit' at any prompt to quit.\n")

while True:
    try:
        # Get experimental values from you
        frame_input = input("Enter a Frame Index to test (e.g., 1, 2, 3): ").strip()
        if frame_input.lower() == 'exit':
            break

        fps_input = input("Enter a target Frame Rate/FPS (e.g., 15, 30, 60): ").strip()
        if fps_input.lower() == 'exit':
            break

        # Parse targets
        target_frame_idx = int(frame_input)
        target_fps = int(fps_input)
        target_interval = calculate_interval(target_fps)

        # 3. Construct the clean 26-byte layout verified via Ghidra
        payload = [0] * 26
        payload[0] = 0x00 # bmHint Low Byte
        payload[1] = 0x00 # bmHint High Byte
        payload[2] = 0x02 # bFormatIndex (Fixed to 2 for MJPEG stream)
        payload[3] = target_frame_idx # Custom resolution selector

        # Split dwFrameInterval into 4 Little-Endian bytes
        payload[4] = target_interval & 0xFF
        payload[5] = (target_interval >> 8) & 0xFF
        payload[6] = (target_interval >> 16) & 0xFF
        payload[7] = (target_interval >> 24) & 0xFF

        print("\n--> Sending Probe command (wValue 0x0100)...")
        dev.ctrl_transfer(0x21, 0x01, 0x0100, 1, payload)

        print("--> Sending Commit command (wValue 0x0200)...")
        dev.ctrl_transfer(0x21, 0x01, 0x0200, 1, payload)

        print("--> Querying updated parameters back from camera...")
        response_bytes = dev.ctrl_transfer(0xA1, 0x81, 0x0100, 1, 26)

        # Display results instantly
        print(parse_updated_payload(response_bytes))
        print("=" * 40 + "\n")

    except ValueError:
        print("Invalid number entered. Please try again.\n")
    except Exception as e:
        print(f"Hardware Error/Stall: {e}\n")
