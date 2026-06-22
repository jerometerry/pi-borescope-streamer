# Recompiling the Raspberry Pi Kernel (eBPF & BTF Support)

To utilize advanced observability tools like eBPF and FlameGraphs, the Linux kernel must be compiled with BPF Type Format (BTF) support. On a default Raspberry Pi OS installation, this requires pulling the kernel source, configuring the tracers, and recompiling the core image.

_Reference: Official Raspberry Pi [Linux Kernel Documentation_](https://www.raspberrypi.com/documentation/computers/linux_kernel.html)

## ⚠️ Important: Hardware Failsafes

**Caution:** Recompiling and replacing the boot kernel carries a high risk of breaking the boot process. Before attempting this, it is highly advised to configure a hardware serial connection so you can read kernel panics or boot failures from the console.

**Serial Debugging on the Raspberry Pi 5:**
The Raspberry Pi 5 features a dedicated UART debug port.

- **The Easy Way:** The official Raspberry Pi [Debug Probe](https://www.raspberrypi.com/products/debug-probe/) provides a plug-and-play solution.
- **The DIY Way:** You can use a standard USB-to-TTL Serial cable (e.g., PL2303G or FTDI). Because the Pi 5 UART port is miniaturized, you will need to wire the cable to a **micro JST SH 1.0mm Pitch 3-Pin Male** connector.

Connect to the Pi from your host machine using a terminal emulator like `minicom` (adjust your `/dev/tty` or `/dev/cu` device path accordingly):

```bash
minicom -D /dev/cu.PL2303G-USBtoUART1220 -b 115200

```

---

## 1. Install Build Dependencies

Install the required build tools, SSL development headers, and ELF utilities needed for kernel and BTF generation.

```bash
sudo apt update
sudo apt install git bc bison flex libssl-dev make libc6-dev libncurses5-dev libelf-dev dwarves

```

## 2. Clone the Kernel Source

Clone the official Raspberry Pi Linux repository. Check your current kernel version using `uname -a` and ensure you are working on the corresponding branch (e.g., `rpi-6.6.y`).

_Note: Use `--depth=1` to prevent downloading gigabytes of unnecessary git history._

```bash
git clone --depth=1 https://github.com/raspberrypi/linux
cd linux

```

## 3. Generate the Base Configuration

Set the kernel variable for the **Raspberry Pi 5 (64-bit)** and generate the default Broadcom 2712 configuration file.

```bash
KERNEL=kernel_2712
make bcm2712_defconfig

```

## 4. Customize the Configuration (eBPF & BTF)

Launch the built-in configuration wizard to safely toggle kernel flags:

```bash
make menuconfig

```

Navigate through the menus and enable the following specific options to ensure full `bpftrace` and FlameGraph compatibility:

1. **Tag Your Custom Kernel:**

- Navigate to: `General setup` -> `Local version - append to kernel release`
- Set it to something identifiable, such as `-bpf` (so your kernel shows as `6.6.y-bpf`).

2. **Enable BTF & Debug Info:**

- Navigate to: `Kernel hacking` -> `Compile-time checks and compiler options` -> `Compile the Kernel with debug info`
- Select: `Rely on the toolchain's implicit default DWARF version`
- Navigate to: `Kernel hacking` -> `Compile-time checks and compiler options` -> `Generate BTF typeinfo`

3. **Enable Uprobes & Dynamic Logging:**

- Navigate to: `Kernel hacking` -> `Tracers` -> `Enable uprobes-based dynamic events`
- Navigate to: `Kernel hacking` -> `Printk and logs` -> `Enable dynamic printk() support`

Save the configuration and exit the wizard.

## 5. Build the Kernel

Compile the kernel image, modules, and Device Tree Blobs (DTBs). Using `-j$(nproc)` ensures the compiler uses all available CPU cores.

```bash
make -j$(nproc) Image.gz modules dtbs

```

_(Note: This process is incredibly CPU-intensive and can take up to an hour depending on your hardware cooling.)_

## 6. Installation & Deployment

Once the build completes successfully, install the new modules to your root filesystem, backup your existing kernel, and deploy the new images to the boot partition.

**1. Install the Kernel Modules:**

```bash
make -j$(nproc)
sudo make modules_install
sudo make install
```

**2. Backup the Current Kernel:**

```bash
sudo cp /boot/firmware/$KERNEL.img /boot/firmware/$KERNEL-backup.img

```

**3. Deploy the New Kernel & Device Trees:**

```bash
sudo cp arch/arm64/boot/Image /boot/firmware/kernel_2712.img
sudo cp arch/arm64/boot/dts/broadcom/*.dtb /boot/firmware/
sudo cp arch/arm64/boot/dts/overlays/*.dtb* /boot/firmware/overlays/
sudo cp arch/arm64/boot/dts/overlays/README /boot/firmware/overlays/

```

**4. Reboot:**

```bash
sudo reboot

```

If the system boots successfully, running `uname -a` will now display your custom `-bpf` tag, and you are ready to start profiling with eBPF.
