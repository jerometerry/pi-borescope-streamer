import usb.core
import usb.util
import sys

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

# Setup payload
payload = [0] * 26
payload[2] = 0x02
payload[3] = target_index
interval = 10000000 // 30
payload[4] = interval & 0xFF
payload[5] = (interval >> 8) & 0xFF
payload[6] = (interval >> 16) & 0xFF
payload[7] = (interval >> 24) & 0xFF

dev.ctrl_transfer(0x21, 0x01, 0x0100, 1, payload)
dev.ctrl_transfer(0x21, 0x01, 0x0200, 1, payload)

# Handshakes
dev.write(0x02, [0xFF, 0x55, 0xFF, 0x55, 0xEE, 0x10])
dev.write(0x01, [0xBB, 0xAA, 0x05, 0x00, 0x00])

try:
    raw_video_data = dev.read(0x81, 1024, timeout=4000)
    print(f"\n--- RAW BYTES DUMP FOR INDEX {target_index} ---")
    print("Hex format:", [hex(x) for x in raw_video_data[:32]])
except Exception as e:
    print("Error:", e)
