# Recompile Kernel Steps

Official Raspberry Pi [Linux Kernel Docs](https://www.raspberrypi.com/documentation/computers/linux_kernel.html)

**Caution**: When recompiling the kernel an installing it onto boot media, it's possible to break booting of the Raspberry PI. Before attempting to recompile the kernel, I'd advise having a serial cable on hand and verifying that you can connect to your Raspberry PI using your terminal emulator of choice.

I purchased a DTECH [USB to TTL Serial cable](https://www.amazon.ca/dp/B0FBM3KWBD). At first I wired it up to via GPIO pins GND. Then I switched to using the default UART port, by cutting off the DuPont connectors and adding a mini micro JST SH 1.0mm Pitch 3-Pin Male connector.

The official Raspberry Pi [Debug Probe](https://www.raspberrypi.com/products/debug-probe/) is your best bet for the least amount of hassle. 

With the USB to TTL Serial Cable attached, I use minicom to connect to the Raspberry PI. 

```bash
minicom -D /dev/cu.PL2303G-USBtoUART1220 -b 115200
```

## Install Necessary Libraries

```bash
sudo apt install git bc bison flex libssl-dev make libc6-dev libncurses5-dev libelf-dev dwarves
```

## Clone Raspberry PI Linux repo

Clone using depth=1 to only pull down the necessary files. The repo is quite large. 

```bash
git clone --depth=1 https://github.com/raspberrypi/linux
cd linux
```

Checkout the branch corresponding to the version of the linux kernel you are running. You can find the current kernel version via the command `uname -a`.

The branch for the latest kernel version is checked out by default. The latest branch at the time of this writing is `rpi-6.18.y`.

## Make Kernel Config

**Raspberry Pi 5 64-bit OS**
```bash
KERNEL=kernel_2712
make bcm2712_defconfig
```

## Edit Kernel Config

You can use the built in config editor wizard via:

```bash
make menuconfig
```

This opens up a console application that lets you tweak settings in a safer way than editing the config file directly. You can still edit the .config file manually if you want. 

It's a good idea to customize local version to include something to identify your custom kernel version: e.g. `-bpf`.

- General setup -> Local version - append to kernel release. 

## Configuring BTF Support

If you want to use eBPF, for example to generate FlameGraphs, you'll need to enable BTF (BPF Type Format) Support. 

- Enable "Kernel hacking -> Compile-time checks and compiler options -> Compile the Kernel with debug info"
  - Select "Rely on the toolchain's implicit default DWARF version"
- Enable "Kernel hacking -> Compile-time checks and compiler options -> Generate BTF typeinfo"
- Enable "Kernel hacking -> Tracers -> Enable uprobes-based dynamic events"

If you want to run BPF / linux perf on custom applications you are building, ensure to add the following flags:
-  `-g`:  Embeds your source code maps into the build
- `-fno-omit-frame-pointer`:  Instructs the compiler to keep the frame pointer register on the stack for every function call.

## Build the kernel, modules, and device trees

**Raspberry Pi 5 64-bit OS**
```bash
make -j6 Image.gz modules dtbs
```

## Install modules

```bash
sudo make -j6 modules_install
```

## Backup Current kernel image

```bash
sudo cp /boot/firmware/$KERNEL.img /boot/firmware/$KERNEL-backup.img
```

## Copy the new kernel and DTBs to the boot partition

```bash
sudo cp arch/arm64/boot/Image.gz /boot/firmware/$KERNEL.img
sudo cp arch/arm64/boot/dts/broadcom/*.dtb /boot/firmware/
sudo cp arch/arm64/boot/dts/overlays/*.dtb* /boot/firmware/overlays/
sudo cp arch/arm64/boot/dts/overlays/README /boot/firmware/overlays/
```

## Reboot

```bash
sudo reboot
```