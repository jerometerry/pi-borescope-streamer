## On-Tree Builds

To build the useeplus v4l2 driver as part of the Kernel build, there's a few steps

### Create /drivers/media/usb/useeplus Folder

The useeplus v4l2 driver belongs in /drivers/media/usb. Create a new folder called useeplus

### Add useeplus .c/.h, Kconfig, Makefile

Copy the useeplus source code, Kconfig file and Makefile into the /useeplus folder under
/drivers/media/usb.

### Add useeplus to /drivers/media/usb/Kconfig

To register the useeplus driver as a module to select during kernel configuration, you need
to register the useeplus/Kconfig file in /drivers/media/usb/Kconfig, by adding the following
line to the `if MEDIA_CAMERA_SUPPORT` / `endif` block:

```bash
source "drivers/media/usb/useeplus/Kconfig"
```

Read the instructions in the comments of that file, and ensure to keep the list alphabetical.

### Add useeplus to /drivers/media/usb/Makefile

To register the useeplus driver as a module to compile during the modules build step of the
kernel build process, add the following line to /drivers/media/usb/Makefile, in the bottom section
that's for all drivers that aren't DVD USB :

```bash
obj-$(CONFIG_USB_USEEPLUS) += useeplus/
```

Read the instructions in the comments of that file, and ensure to keep the list alphabetical.

### Add `Useeplus Protocol USB Camera support` to Kernel .config file

If you are using `make menuconfig`, then enable `Useeplus Protocol USB Camera support` under

`Device Drivers ➔ Multimedia support ➔ Media drivers ➔ Media USB Adapters`

If you prefer editing the .config file manually

```bash
#
# Webcam devices
#

...

CONFIG_USB_USEEPLUS=m

...
```

### Compiling the kernel

```bash
# run this in the linux tree root folder

# Build the kernel and modules
make -j6 Image.gz modules dtbs

# Install the modules
sudo make -j6 modules_install

# Backup the previous (working) kernel, just in case.
# change kernel_2712 to your actual kernel name.
sudo cp /boot/firmware/kernel_2712.img /boot/firmware/kernel_2712-backup.img

# Drop in the brand spanking new kernel image
sudo cp arch/arm64/boot/Image.gz /boot/firmware/kernel_2712.img

# Copy over the Device Tree Sources configs so kernel bootloader can configure
# the onboard hardware (CPU, GPIO, peripherals, etc.)
sudo cp arch/arm64/boot/dts/broadcom/*.dtb /boot/firmware/
sudo cp arch/arm64/boot/dts/overlays/*.dtb* /boot/firmware/overlays/
sudo cp arch/arm64/boot/dts/overlays/README /boot/firmware/overlays/

# Reboot, and the useeplus driver should load automatically when plugging in a
`Geek szitman supercamera`.
sudo reboot
```

### Deploying Modules

Driver modules are hot reloadable, so we don't have to recompile the kernel after making driver
changes. Just recompile and install the modules

```bash
# run this in the linux tree root folder

# compile all the modules
make modules -j6
# install the drivers
sudo make modules_install
```

### Deploying useeplus Driver module independently

```bash
# run this in the linux tree root folder

# compile all the modules
make modules -j6

# install just the useeplus driver module
sudo cp drivers/media/usb/useeplus/useeplus.ko /lib/modules/$(uname -r)/kernel/drivers/media/usb/useeplus/

# have the kernel reload updated modules
sudo depmod -a
```
