# Integrating `useeplus` into the Linux Kernel Tree

This document outlines the process for integrating the `useeplus` V4L2 driver directly into the official Linux kernel source tree. This is the preferred method for long-term maintenance and kernel-level testing.

## 1. Registering the Driver in the Kernel Tree

### Create the Source Directory

Place your `useeplus` source files (`.c`, `.h`), `Kconfig`, and `Makefile` into the designated multimedia directory:
`/drivers/media/usb/useeplus/`

### Update the USB Media Subsystem

You must register the driver with the Linux configuration and build systems to ensure it is recognized during the kernel compilation process.

1. **Register with `Kconfig`:** Add the following line to `/drivers/media/usb/Kconfig` inside the `if MEDIA_CAMERA_SUPPORT` block:

```bash
source "drivers/media/usb/useeplus/Kconfig"

```

_Ensure you maintain the alphabetical order within the file._ 2. **Register with `Makefile`:** Add the following entry to the bottom of `/drivers/media/usb/Makefile` (within the section for USB camera drivers):

```bash
obj-$(CONFIG_USB_USEEPLUS) += useeplus/

```

## 2. Kernel Configuration

Once registered, you can toggle the driver during the configuration phase.

- **GUI Method:** Use `make menuconfig` and enable the driver at:
  `Device Drivers` ➔ `Multimedia support` ➔ `Media drivers` ➔ `Media USB Adapters` ➔ `Useeplus Protocol USB Camera support`
- **Manual Method:** Manually edit your `.config` file to include:

```text
CONFIG_USB_USEEPLUS=m

```

---

## 3. Kernel Compilation & Installation

To build the entire kernel tree, execute these steps from the root of your Linux source directory:

```bash
# 1. Build the kernel image, modules, and device trees
make -j$(nproc) Image.gz modules dtbs

# 2. Install the kernel modules to the system
sudo make -j$(nproc) modules_install

# 3. Backup the current boot kernel
sudo cp /boot/firmware/kernel_2712.img /boot/firmware/kernel_2712-backup.img

# 4. Deploy the new image and device tree blobs (DTBs)
sudo cp arch/arm64/boot/Image.gz /boot/firmware/kernel_2712.img
sudo cp arch/arm64/boot/dts/broadcom/*.dtb /boot/firmware/
sudo cp arch/arm64/boot/dts/overlays/*.dtb* /boot/firmware/overlays/
sudo cp arch/arm64/boot/dts/overlays/README /boot/firmware/overlays/

# 5. Reboot to initialize the new kernel
sudo reboot

```

---

## 4. Rapid Driver Iteration

You do not need to recompile the entire kernel to test changes to your driver. Use these workflows to rebuild and reload the module independently.

### Recompiling & Reinstalling All Modules

If you have made widespread changes, rebuild all modules from the root tree:

```bash
make modules -j$(nproc)
sudo make modules_install

```

### Targeted Rebuild (Recommended)

To rebuild only the `useeplus` module and its dependencies, execute the build command specifically against the driver directory:

```bash
# Compile just the useeplus module
make M=drivers/media/usb/useeplus modules

# Deploy just the useeplus module
sudo make M=drivers/media/usb/useeplus modules_install

```

---

## 5. Build Environment via Docker (macOS)

For developers compiling the Linux kernel on macOS, you can utilize an Ubuntu-based Docker container to maintain a consistent build environment.

```bash
# 1. Create a persistent volume to hold the source tree
docker volume create kernel-workspace

# 2. Launch and attach to the build environment
docker run --rm -it -v kernel-workspace:/kernel-src local-kernel-builder bash

# 3. Inside the container:
git clone https://github.com/jerometerry/linux.git
cd linux
git checkout -b useeplus_v4l2 origin/useeplus_v4l2

# 4. Generate config and trigger build
make defconfig
make -j$(nproc)

```
