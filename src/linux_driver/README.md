# Supercamer Linux Driver

## Prerequisites

```bash
sudo apt update
sudo apt install build-essential linux-headers-$(uname -r)
```

## Building

```bash
make
```

## Verifying Build

```bash
modinfo supercamera.ko
```

## Load Linux Driver 

```bash
sudo insmod supercamera.ko
```

## Verify Linux Driver Loaded 

```bash
dmesg | tail -n 20
```

```shell-session
jterry@authentic-nerd:~/github/pi-borescope-streamer/src/linux_driver $ dmesg | tail -n 20
[441627.616072] usb 3-1: SerialNumber: 022018050100030
[442625.102911] supercamera 3-1:1.0: Ignoring Interface 0 (iAP authentication layer)
[442625.102936] supercamera 3-1:1.1: Geek szitman supercamera core Interface 1 detected.
[442625.105467] supercamera 3-1:1.1: Sending hardware initialization tokens...
[442625.105500] supercamera 3-1:1.1: Sending streaming request tokens...
[442625.213470] supercamera 3-1:1.1: Hardware pipes successfully initialized and streaming.
[442625.213530] usbcore: registered new interface driver supercamera
[442640.982909] usb 3-1: USB disconnect, device number 15
[442640.983062] supercamera 3-1:1.1: Geek szitman supercamera disconnected.
[442659.019673] usb 3-1: new high-speed USB device number 16 using xhci-hcd
[442659.161440] usb 3-1: New USB device found, idVendor=0329, idProduct=2022, bcdDevice= 1.00
[442659.161447] usb 3-1: New USB device strings: Mfr=1, Product=2, SerialNumber=3
[442659.161452] usb 3-1: Product: supercamera
[442659.161457] usb 3-1: Manufacturer: Geek szitman
[442659.161460] usb 3-1: SerialNumber: 022018050100030
[442659.171380] supercamera 3-1:1.0: Ignoring Interface 0 (iAP authentication layer)
[442659.172741] supercamera 3-1:1.1: Geek szitman supercamera core Interface 1 detected.
[442659.175596] supercamera 3-1:1.1: Sending hardware initialization tokens...
[442659.175631] supercamera 3-1:1.1: Sending streaming request tokens...
[442659.679777] supercamera 3-1:1.1: Hardware pipes successfully initialized and streaming.
```