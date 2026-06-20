## Setup

Download a cloud image

https://cloud-images.ubuntu.com/minimal/releases/noble/release-20260105/ubuntu-24.04-minimal-cloudimg-arm64.img

## Create Files

```bash
mkdir useeplus-qemu
cd useeplus-qemu

touch launch.sh
chmod +x launch.sh

touch user-data
touch meta-data
```

## Add File Content

### user-data

```yaml
#cloud-config
ssh_pwauth: True
chpasswd:
  list: |
    ubuntu:password
  expire: False
```

### launch.sh

```bash
#!/bin/bash

VENDOR_ID="0x0329"
PRODUCT_ID="0x2022"

echo "Starting QEMU Linux Emulator with USB Pass-through..."

sudo qemu-system-aarch64 \
  -m 2G \
  -smp 2 \
  -cpu host \
  -accel hvf \
  -machine virt \
  -bios "${FIRMWARE_DIR}/edk2-aarch64-code.fd" \
  -drive file=ubuntu-24.04-minimal-cloudimg-arm64.img,format=qcow2,if=virtio \
  -drive file=cloud-init.iso,format=raw,media=cdrom \
  -netdev user,id=net0,hostfwd=tcp::2222-:22 \
  -device virtio-net-pci,netdev=net0 \
  -device qemu-xhci,id=xhci \
  -device usb-host,bus=xhci.0,vendorid=${VENDOR_ID},productid=${PRODUCT_ID} \
  -nographic
```

## Create cloud-init.iso File

```bash
python3 -c "
import os
os.system('dd if=/dev/zero of=cloud-init.iso bs=1024 count=100')
os.system('diskutil erasevolume MS-DOS CIDATA \$(hdid -nomount cloud-init.iso | awk \"{print \$1}\")')
"

cp user-data /Volumes/CIDATA/
cp meta-data /Volumes/CIDATA/
diskutil eject /Volumes/CIDATA
```

## Launch

```bash
./launch.sh
```

MacOS will prompt for your login credentials before the emulator will start. Once the emulator
starts, it will boot linux and eventually show the login prompt.

Login credentials
username: ubuntu
password: password

## Increase Volume Size

```bash
sudo poweroff
```

```bash
qemu-img resize ubuntu-24.04-minimal-cloudimg-arm64.img +20G
```
