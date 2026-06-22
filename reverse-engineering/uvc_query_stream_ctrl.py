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

dev.set_configuration()

# Emulate: uvc_query_stream_ctrl(param_1, param_2, '\x01', 0x81)
# bmRequestType: 0xA1 (Class Interface In)
# bRequest:      0x81 (GET_CUR)
# wValue:        0x0100 (VS_PROBE_CONTROL)
# wIndex:        1 (UP_VIDEO_INTERFACE)
# Size:          26 bytes (0x1a)
try:
    print("Sending dynamic descriptor configuration query...")
    config_bytes = dev.ctrl_transfer(0xA1, 0x81, 0x0100, 1, 26)

    print("\n--- RAW DATA ARRAY RETURNED ---")
    print([hex(b) for b in config_bytes])

    print("\n--- MAPPED VALUES FROM STRUCT ---")
    print(f"Format Index (Byte 2): {config_bytes[2]}")
    print(f"Frame Index  (Byte 3): {config_bytes[3]}")

    interval = config_bytes[4] | (config_bytes[5] << 8) | (config_bytes[6] << 16) | (config_bytes[7] << 24)
    print(f"Frame Interval (Bytes 4-7): {interval} units")
    if interval > 0:
        print(f"Calculated FPS: {10000000 / interval:.2f} FPS")

except Exception as e:
    print("Transfer error. The pipeline requires alternate setup configuration keys:", e)
