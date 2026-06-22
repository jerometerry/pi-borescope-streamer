import usb.core
import usb.util
import sys

# Connect to the device via your exact PID/VID
dev = usb.core.find(idVendor=0x0329, idProduct=0x2022)
if dev is None:
    print("Device not found")
    sys.exit(1)

# Corrected interface detach iteration loop
for intf in dev[0]:
    if dev.is_kernel_driver_active(intf.bInterfaceNumber):
        try:
            dev.detach_kernel_driver(intf.bInterfaceNumber)
            print(f"Detached kernel driver on interface {intf.bInterfaceNumber}")
        except Exception as e:
            print(f"Could not detach interface {intf.bInterfaceNumber}: {e}")

# Explicitly set the active USB configuration
try:
    dev.set_configuration()
except usb.core.USBError as e:
    print(f"Could not set configuration: {e}")
    sys.exit(1)

# Claim the specific video control interface (Interface 1)
# Without this, the OS may reject control transfers targeted at wIndex = 1
try:
    usb.util.claim_interface(dev, 1)
    print("Successfully claimed interface 1 control plane")
except usb.core.USBError as e:
    print(f"Could not claim interface 1: {e}")
    sys.exit(1)

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
