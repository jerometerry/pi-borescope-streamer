## Zig Links

You really should install the pre-built binaries. Here's links to the official Zig pages where you
should get the correct downloads and install procedures.

- **Zig website**: https://ziglang.org/
- **Zig repo**: https://codeberg.org/ziglang/zig
- **Zig docs**: https://ziglang.org/documentation/0.16.0/
- **Zig releases**: https://ziglang.org/download/

## Configuring Swap Space

That's the spirit! ignore the official docs and built it with what ya got.

I tried compiling Zig from source on my Raspberry Pi 5 which has 4 GB of RAM and a 2 GB swap size.
My system came to a standstill because `make install` consumed all the RAM and swap. I ran vmstat
when I noticed Mem and Swap were maxed out in htop. Here's the output

```shell-content
jterry@authentic-nerd:~ $ vmstat 5
procs -----------memory---------- ---swap-- -----io---- -system-- -------cpu-------
 r  b   swpd   free   buff  cache   si   so    bi    bo   in   cs us sy id wa st gu
 0  7 2096880  16224   3024 136928 1367 5271 104582  5637 3721    9  5  3 66 26  0  0
 0  8 2096784  14672   2832 131424   19    0 302398     3 10589 11427  0  6 11 83  0  0
 0  5 2096768  17568   2544 129648    3    0 301738     0 10669 11841  0  6  7 87  0  0
```

I have a 500GB SSD as my primary drive for my Raspberry Pi (don't build Zig from source if you are
using a microSD card.) Increasing swap was a simple solution to get this to build.

Here's the vmstat output while running `make install -j2`

```shell-content
jterry@authentic-nerd:~ $ vmstat 5
procs -----------memory---------- ---swap-- -----io---- -system-- -------cpu-------
 r  b   swpd   free   buff  cache   si   so    bi    bo   in   cs us sy id wa st gu
 2  0 338512 212384  12496 852240 1184 2921 60529 17134 2510    6  7  2 74 17  0  0
 1  0 332352 227472  12016 842240 3552 2934  4099  2934 2353 1767 23  0 74  2  0  0
 1  0 319264 220208  12016 844128 3354    0  3389     0 1182  511 23  0 75  1  0  0
```

### Increase Swap Space

```bash
# Disable swap
sudo swapoff -a

# Allocate a 16 GB file (16 * 1024 = 16384 blocks of 1M)
sudo dd if=/dev/zero of=/swapfile bs=1M count=16384 status=progress

# Set permissions
sudo chmod 600 /swapfile

# Format as swap
sudo mkswap /swapfile

# Activate swap
sudo swapon /swapfile

# Verify
free -h
```

Add this to the bottom of /etc/fstab to mount swap at boot

```bash
echo '/swapfile none swap sw 0 0' | sudo tee -a /etc/fstab
```

### Tune Swap for SSDs

Create a file `99-swap-optimizations.conf` in `/etc/sysctl.d`. Prefixing the filename with 99
ensures that it overrides system default swap configurations.

```bash
# If you're a vim user
sudo vim /etc/sysctl.d/99-swap-optimizations.conf

# If you're a nano user
sudo nano /etc/sysctl.d/99-swap-optimizations.conf
```

Add these settings

```ini
# Encourage smoother background swapping for idle pages on SSD
vm.swappiness=20

# Balance file system cache pruning with virtual memory usage
vm.vfs_cache_pressure=50
```

Save the file. Reload settings by running

```bash
sudo sysctl --system
```

## Install Dependencies

### LLVM

```bash
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh
```

### Build Tools

```bash
sudo apt install -y libclang-22-dev lld-22 liblld-22-dev
```

## Configuring The Zig Build

Download sources direct from Zig.

https://ziglang.org/download/

```bash
wget https://ziglang.org/builds/zig-0.17.0-dev.902+7255f3e72.tar.xz
tar -xf zig-0.17.0-dev.902+7255f3e72.tar.xz

cd zig-0.17.0-dev.902+7255f3e72

mkdir build
cd build
```

## Hack Zig if you have less than 8GB of RAM

The zig build process requires 8GB of RAM. Well, what do you do if you are on a Raspberry Pi 5 with
4GB of RAM and not a lot of common sense? You hack the Zig build!

Edit Maker.zig in the /lib/compiler folder.

```bash
# From the root source directory:
vim lib/compiler/Maker.zig

# Or from the build folder:
vim ../lib/compiler/Maker.zig
```

Look for these lines

```zig
// Check that we have enough memory to complete the build.
var any_problems = false;
```

Insert this line to tell Maker that we have 16GB of RAM. Well, it's technically swap space but
what could possibly go wrong? lol. Seriously, I'm joking. Don't do this. Build on the correct
hardware or download pre-built binaries. lol.

```zig
maker.available_rss = 16_000_000_000;
```

## Run CMake

You may need to add the path to the LLVM directory if cmake can't find it

```bash
cmake -DCMAKE_PREFIX_PATH="/usr/lib/llvm-22" ..
```

## Run make install

```bash
make install -j2
```
