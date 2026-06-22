# Useeplus Linux Driver Development

This guide covers the workflow for compiling, loading, testing, and debugging the `useeplus_v4l2` kernel module on a Raspberry Pi.

## 1. Prerequisites & Environment Setup

To compile kernel modules, you need the build tools and the specific Linux headers matching your active kernel.

```bash
sudo apt update
sudo apt install build-essential linux-headers-$(uname -r)

```

### Linux Kernel Style Guide

When writing kernel drivers, it is highly recommended to strictly adhere to the official Linux Kernel coding standards.

- [Linux Kernel Coding Style](https://docs.kernel.org/process/coding-style.html)
- [Driver API Documentation](https://docs.kernel.org/driver-api/index.html)
- [Writing USB Drivers](https://docs.kernel.org/driver-api/usb/writing_usb_driver.html)
- [Kernel .clang-format Configuration](https://github.com/torvalds/linux/blob/master/.clang-format)

To run the official kernel style checker (`checkpatch.pl`), you can clone a shallow copy of the Raspberry Pi Linux repository. Ensure you check out the branch matching your current kernel version. _(See the [Recompiling Kernel](https://www.google.com/search?q=../../RecompileKernel.md) documentation for advanced configuration details)._

```bash
# Clone the repository to get the scripts
git clone --depth=1 https://github.com/raspberrypi/linux

# Run the style checker against your source file
./linux/scripts/checkpatch.pl --file ./useeplus_v4l2.c

```

## 2. Building the Driver

The module utilizes the standard `kbuild` system.

```bash
make clean
make

# Verify the compiled module targets the correct architecture
modinfo useeplus_v4l2.ko

```

## 3. Module Management (Load & Unload)

When developing, you will frequently need to unload the old driver and insert the newly compiled version.

```bash
# 1. Ensure nothing is currently locking the video device
sudo lsof /dev/video0

# 2. Unload the existing module (if it is currently running)
sudo rmmod useeplus_v4l2

# 3. Ensure required kernel dependencies are loaded
sudo modprobe videobuf2_vmalloc

# 4. Insert the newly compiled module
sudo insmod ./useeplus_v4l2.ko

# 5. Verify the module successfully registered
lsmod | grep useeplus_v4l2

```

## 4. Verification & Testing

Once the hardware is physically plugged in and the driver is loaded, verify the Video4Linux subsystem recognizes it.

```bash
# List all registered V4L2 devices
v4l2-ctl --list-devices

# Run the official compliance test suite against the new node
v4l2-compliance -d /dev/video0

```

### Capturing Test Snapshots

You can use `ffmpeg` to pull raw frames directly from the V4L2 node to verify the payload is intact:

```bash
ffmpeg -f v4l2 -i /dev/video0 -vframes 10 -update 1 snapshot.jpg

```

### Launching the MJPEG Stream

_Note: While you can use third-party tools like `ustreamer` for benchmarking, you can now natively pipe this V4L2 node into the project's custom user-space streaming server._

```bash
# Using the custom zero-allocation server:
./build/v4l2_mjpeg_server

# Alternatively, testing with uStreamer:
ustreamer -d /dev/video0 -r 640x480 -f 30 -m MJPEG -p 8080 --host 0.0.0.0

```

## 5. Debugging

The kernel ring buffer (`dmesg`) is your primary debugging tool for driver development.

```bash
# Follow the live kernel logs in a dedicated terminal window
sudo dmesg -w

# Alternatively, just print the last 20 driver events
dmesg | tail -n 20

```

### Changing Kernel Log Levels

If your `pr_debug()` or `dev_dbg()` statements are not appearing in `dmesg`, you may need to temporarily elevate the kernel's printk logging verbosity.

```bash
# View the current log levels
cat /proc/sys/kernel/printk

# Elevate to debug logging
sudo sysctl -w kernel.printk="8 4 1 3"

# Restore default logging when finished
sudo sysctl -w kernel.printk="3 4 1 3"
```

### Stress Testing Driver

For detecting memory leaks in the useeplus v4l2 driver, you can enable CONFIG_DEBUG_KMEMLEAK in
the kernel configuration (.config) file. Once enabling this, you need to recompile the kernel,
install the kernel and modules, and reboot.

To access the memory leak detection data, you need to mount debugfs

```bash
mount -t debugfs nodev /sys/kernel/debug
```

Here's a set of scripts to run a stress test on the driver, and look for memory leaks

```bash
# Clear out benign system alerts cached from the boot sequence
echo clear | sudo tee /sys/kernel/debug/kmemleak

# Run a high-intensity stream loop for 5 minutes (approx. 9000 frames)
# This will push your up_buf_queue, workqueue decoder, and USB pipes to their limits
v4l2-ctl --device=/dev/video0 --stream-mmap --stream-to=/dev/null --stream-count=9000

# Instruct the kernel to scan its active allocation references immediately
echo scan | sudo tee /sys/kernel/debug/kmemleak

# Inspect the output log
sudo cat /sys/kernel/debug/kmemleak
```

### Finding Duplicate Modules

```bash
# Find copies of useeplus modules
find /lib/modules/$(uname -r)/ -name "useeplus.ko*"
```

```shell-content
$ find /lib/modules/$(uname -r)/ -name "useeplus.ko*"
/lib/modules/6.18.35-v8-16k-useeplus+/updates/useeplus.ko.xz
/lib/modules/6.18.35-v8-16k-useeplus+/kernel/drivers/media/usb/useeplus/useeplus.ko.xz
```

```bash
# Remove module that's not on-tree
sudo rm -f /lib/modules/$(uname -r)/updates/useeplus.ko.xz

# Rebuild module deps
sudo depmod -a

# Check module info. intree should be Y, vermagic should start with kernel version
modinfo useeplus | grep -E "filename|intree|vermagic"
```
