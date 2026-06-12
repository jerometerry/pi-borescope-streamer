# Useeplus Linux Driver

## Prerequisites

```bash
sudo apt update
sudo apt install build-essential linux-headers-$(uname -r)
```

## Building

```bash
make clean
make
```

## Verifying Build

```bash
modinfo useeplus_v4l2.ko
```

## Load Linux Driver 

```bash
# Verify no useeplus_v4l2 module is found
lsmod | grep useeplus_v4l2

# If there is, remove the existing useeplus_v4l2 module to deploy a new version
sudo rmmod useeplus_v4l2 2>/dev/null

# check if videobuf2_vmalloc module is loaded
lsmod | grep videobuf2_vmalloc

# If it's not, load it
sudo modprobe videobuf2_vmalloc

# Load the new useeplus_v4l2 module
sudo insmod ./useeplus_v4l2.ko 2>/dev/null

# Verify useeplus_v4l2 module is loaded
lsmod | grep useeplus_v4l2

# Verify driver was loaded by viewing dmesg output
dmesg | tail -n 20
```

## Verify useeplus_v4l2 Registered with Video4Linux

Plug in the useeplus_v4l2 into a USB port on the Raspberry Pi. Run this command to confirm the useeplus_v4l2 is listed

```bash
v4l2-ctl --list-devices
```

## Capturing Snapshots with FFMPEGs

```bash
ffmpeg -f v4l2 -i /dev/video0 -vframes 10 -update 1 snapshot.jpg
```

## Launch the uStreamer Server


Start the MJPEG HTTP server, pointing it to the v4l2 device the useeplus_v4l2 was assigned ( e.g. `/dev/video0`). 
Binding the host to `0.0.0.0` ensures the stream is accessible from any device on your local network:

```bash
ustreamer -d /dev/video0 -r 640x480 -f 30 -m MJPEG -p 8080 --host 0.0.0.0


```

## Unload Linux Driver 

```bash
sudo rmmod useeplus_v4l2
```

## Verify Linux Driver Loaded 

```bash
dmesg | tail -n 20
```

```bash
sudo dmesg -w
```