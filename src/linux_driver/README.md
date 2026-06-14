# Useeplus Linux Driver

## Prerequisites

```bash
sudo apt update
sudo apt install build-essential linux-headers-$(uname -r)
```

## Linux Kernel Style Check

https://docs.kernel.org/process/coding-style.html

https://docs.kernel.org/driver-api/index.html

https://docs.kernel.org/driver-api/usb/writing_usb_driver.html

The Linux Kernel has it's own style checker. It's good practice when writing kernel drivers to stick with the 
Linux Kernel requirements.

On the Raspberry Pi, you can check out the official linux repo, which contains the checkpath.pl script.

See my [Recompiling Kernel](../../RecompileKernel.md) docs for Raspberry Pi specific details on recompiling the kernel.
If you're tinkering with building linux drivers, recompiling the kernel may be necessary to configure it for your needs. 

**Checkout the Raspberry Pi Linux Repo**

Make sure to switch to the branch for your specific kernel version.

```bash
git clone --depth=1 https://github.com/raspberrypi/linux
```

**Running Linux Kernel Style Check**

```bash
$/github/linux/scripts/checkpatch.pl --file ./useeplus_v4l2.c 
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
# Watch dmesg in a separate terminal window
sudo dmesg -w

# Check what's using the module
sudo lsof /dev/video0

# Verify no useeplus_v4l2 module is found
lsmod | grep useeplus_v4l2

# If there is, remove the existing useeplus_v4l2 module to deploy a new version
sudo rmmod useeplus_v4l2

# check if videobuf2_vmalloc module is loaded
lsmod | grep videobuf2_vmalloc

# If it's not, load it
sudo modprobe videobuf2_vmalloc

# Load the new useeplus_v4l2 module
sudo insmod ./useeplus_v4l2.ko

# Verify useeplus_v4l2 module is loaded
lsmod | grep useeplus_v4l2

# Verify driver was loaded by viewing dmesg output
dmesg | tail -n 20
```

## Changing Log Level 

```bash
# View current log levels
cat /proc/sys/kernel/printk
```

```bash
# Set to debug logging for testing
sudo sysctl -w kernel.printk="8 4 1 3"
```

```bash
# Restore default logging
sudo sysctl -w kernel.printk="3 4 1 3"
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