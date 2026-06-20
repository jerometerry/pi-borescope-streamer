# How to Compile Zig on a Pi (And Why You Shouldn't)

> **⚠️ Disclaimer:** Please, just install the pre-built binaries. I am documenting this purely for my own amusement. Do not do this.

Here are the links to the official Zig pages where you should get the correct downloads and follow the sane, supported installation procedures:

- **Zig Website**: [https://ziglang.org/](https://ziglang.org/)
- **Zig Repo**: [https://codeberg.org/ziglang/zig](https://codeberg.org/ziglang/zig)
- **Zig Docs**: [https://ziglang.org/documentation/0.16.0/](https://ziglang.org/documentation/0.16.0/)
- **Zig Releases**: [https://ziglang.org/download/](https://ziglang.org/download/)

---

## The "Hold My Beer" Approach

That's the spirit! Ignore the official docs and build it with what ya got.

I tried compiling Zig from source on my Raspberry Pi 5, which has 4 GB of RAM and a 2 GB swap size. My system immediately came to a standstill because `make install` violently consumed every byte of RAM and swap available.

I ran `vmstat` when I noticed Mem and Swap were maxed out in `htop`. Here is the output of a Pi gasping for air:

```text
jterry@authentic-nerd:~ $ vmstat 5
procs -----------memory---------- ---swap-- -----io---- -system-- -------cpu-------
 r  b   swpd   free   buff  cache   si   so    bi    bo   in   cs us sy id wa st gu
 0  7 2096880  16224   3024 136928 1367 5271 104582  5637 3721    9  5  3 66 26  0  0
 0  8 2096784  14672   2832 131424   19    0 302398     3 10589 11427  0  6 11 83  0  0

```

I have a 500GB SSD as my primary drive (seriously, **do not** build Zig from source if you are running off a microSD card—you will melt it). Increasing the swap file size was a simple brute-force solution to force this build through.

Here is the `vmstat` output later on while successfully running `make install -j2` with the expanded swap:

```text
jterry@authentic-nerd:~ $ vmstat 5
procs -----------memory---------- ---swap-- -----io---- -system-- -------cpu-------
 r  b   swpd   free   buff  cache   si   so    bi    bo   in   cs us sy id wa st gu
 2  0 338512 212384  12496 852240 1184 2921 60529 17134 2510    6  7  2 74 17  0  0
 1  0 332352 227472  12016 842240 3552 2934  4099  2934 2353 1767 23  0 74  2  0  0

```

---

## Configuring Swap Space

Let's carve out 16 GB of swap space.

```bash
# Disable existing swap
sudo swapoff -a

# Allocate a 16 GB file (16 * 1024 = 16384 blocks of 1M)
sudo dd if=/dev/zero of=/swapfile bs=1M count=16384 status=progress

# Set safe permissions
sudo chmod 600 /swapfile

# Format and activate
sudo mkswap /swapfile
sudo swapon /swapfile

# Verify your new massive swap file
free -h

```

To make this permanent across reboots, append it to your `fstab`:

```bash
echo '/swapfile none swap sw 0 0' | sudo tee -a /etc/fstab

```

### Tune Swap for SSDs

We need to tweak the kernel so we don't thrash the SSD to death. Create a file named `99-swap-optimizations.conf` in `/etc/sysctl.d`. Prefixing the filename with `99` ensures it overrides the system defaults.

```bash
sudo nano /etc/sysctl.d/99-swap-optimizations.conf

```

Add these settings:

```ini
# Encourage smoother background swapping for idle pages on SSD
vm.swappiness=20

# Balance file system cache pruning with virtual memory usage
vm.vfs_cache_pressure=50

```

Save the file and reload the settings:

```bash
sudo sysctl --system

```

---

## Install Dependencies

You will need the LLVM toolchain and some build tools.

```bash
# Pull down the LLVM installer
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh

# Install the build tools
sudo apt install -y libclang-22-dev lld-22 liblld-22-dev

```

---

## Configuring The Zig Build

Download the source directly from Zig. _(Check [https://ziglang.org/download/](https://ziglang.org/download/) for the latest dev build)._

```bash
wget https://ziglang.org/builds/zig-0.17.0-dev.902+7255f3e72.tar.xz
tar -xf zig-0.17.0-dev.902+7255f3e72.tar.xz

cd zig-0.17.0-dev.902+7255f3e72
mkdir build
cd build

```

---

## The Hack (If you have less than 8GB of RAM)

The Zig build process has a hard requirement of 8GB of RAM. Well, what do you do if you are on a Raspberry Pi 5 with 4GB of RAM and a severe lack of common sense?

You lie to the compiler.

We need to edit `Maker.zig` in the compiler folder.

```bash
# From inside your build folder:
nano ../lib/compiler/Maker.zig

```

Scroll down until you find these lines:

```zig
// Check that we have enough memory to complete the build.
var any_problems = false;

```

Right below that, insert this line to tell Maker that we absolutely possess 16GB of high-speed RAM. It is technically SSD swap space, but what could possibly go wrong?

```zig
maker.available_rss = 16_000_000_000;

```

> **Seriously:** I am joking. Do not do this. Build on the correct hardware or download the pre-built binaries.

---

## Light the Fire

Time to build. You may need to explicitly point CMake to the LLVM directory if it can't find it automatically.

```bash
# Configure the build
cmake -DCMAKE_PREFIX_PATH="/usr/lib/llvm-22" ..

# Start the compilation (restrict to 2 jobs so the Pi doesn't completely lock up)
make install -j2

```

Now, walk away. Make a coffee. Watch a movie. It is going to take a while.
