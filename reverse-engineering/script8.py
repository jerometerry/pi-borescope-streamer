# save as switch_test.py
import usb.core
import usb.util
import time

dev = usb.core.find(idVendor=0x0329, idProduct=0x2022)
dev.set_configuration()
dev.set_interface_altsetting(interface=1, alternate_setting=1)

# Resolution Mapping Dictionary discovered via experimentation
resolution_map = {
    1: "640x480 (VGA)",
    2: "320x240 (Low-Res)",
    3: "1280x720 (720p HD)"
}

for idx, name in resolution_map.items():
    print(f"\n[+] Requesting Camera Mode: {name} via Index {idx}")

    # Standard 26-byte UVC-style structural configuration payload
    payload = [0x00] * 26
    payload[2] = 0x02  # MJPEG Format Index
    payload[3] = idx   # Target Frame Index

    # Calculate 30 FPS timing units (333333 = 0x00051615)
    payload[4:8] = [0x15, 0x16, 0x05, 0x00]

    # Send configuration controls down to Endpoint 0
    dev.ctrl_transfer(0x21, 0x01, 0x0100, 1, payload) # PROBE
    dev.ctrl_transfer(0x21, 0x01, 0x0200, 1, payload) # COMMIT

    # Initialize your custom transport engine
    dev.write(0x02, [0xFF, 0x55, 0xFF, 0x55, 0xEE, 0x10]) # iAP Auth
    dev.write(0x01, [0xBB, 0xAA, 0x05, 0x00, 0x00])       # Start video command

    print(f"    Mode successfully initiated in hardware pipeline.")
    time.sleep(1) # Give the sensor array a second to stabilize
