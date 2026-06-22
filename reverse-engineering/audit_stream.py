# Save as endian_audit.py
import sys

def audit_stream(file_path):
    with open(file_path, 'rb') as f:
        data = f.read()

    file_len = len(data)
    offset = 0

    print("Offset_Dec, Alignment, Detected_Device_ID")
    print("-" * 45)

    while offset < file_len - 3:
        # Check Orientation A (Your current scanner pattern)
        if data[offset] == 0xBB and data[offset+1] == 0xAA:
            dev_id = data[offset+2]
            print(f"{offset}, Found [BB AA] -> Next Byte: 0x{dev_id:02X}")
            offset += 2
            continue

        # Check Orientation B (True Little-Endian 0xBBAA short over the wire)
        if data[offset] == 0xAA and data[offset+1] == 0xBB:
            dev_id = data[offset+2]
            print(f"{offset}, Found [AA BB] -> Next Byte: 0x{dev_id:02X}")
            offset += 2
            continue

        offset += 1

if __name__ == "__main__":
    audit_stream(sys.argv[1])
