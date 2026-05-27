# Profiling using BPF and FlameGraphs

Finding performance bottlenecks in C++ code is not as complicated as you might think, using modern tools such as BPF and FlameGraphs. The most complicated part is configuring the Linux kernel to enable BTF Type Info and uprobes. This may involve recompiling the kernel - for example on a Raspberry PI with the default OS install.

Here's a quick primer on [recompiling the Raspberry Pi Kernel](RecompileKernel.md) to add the needed options for BPF.

## Linux Kernel Configs

If you want to use eBPF, for example to generate FlameGraphs, you'll need to enable BTF (BPF Type Format) Support. Here's the options that need to be configured in Raspberry Pi kernel configs when re-compiling, if you are using `make menuconfig` to generate the .config file. 

- Enable "Kernel hacking -> Compile-time checks and compiler options -> Compile the Kernel with debug info"
  - Select "Rely on the toolchain's implicit default DWARF version"
- Enable "Kernel hacking -> Compile-time checks and compiler options -> Generate BTF typeinfo"
- Enable "Kernel hacking -> Tracers -> Enable uprobes-based dynamic events"

## Compiler Flags

If you want to run BPF / linux perf on custom applications you are building, ensure to add the following compiler flags:
-  `-g`:  Embeds your source code maps into the build
- `-fno-omit-frame-pointer`:  Instructs the compiler to keep the frame pointer register on the stack for every function call.

Here's an example of `CXXFLAGS` from a Makefile with these compiler options specified

```
CXXFLAGS := -std=c++23 -Wall -Wextra -O2 -I$(BUILD_DIR) -mcpu=cortex-a76 -mtune=cortex-a76 -flto=auto -g -fno-omit-frame-pointer
```

## Clone Brendan Gregg's FlameGraph Repo

```
git clone https://github.com/brendangregg/FlameGraph.git
```

## Install bpftrace

```
sudo apt update
sudo apt install bpftrace
```

## Install linux-perf

```
sudo apt update
sudo apt install linux-perf
```

## Generate Memory Allocation FlameGraph

```bash
SERVER_PID=$(pgrep -f ./build/server)

echo "Generating memory allocation flame graph for PID $SERVER_PID. Put the server under load. Ctrl+C to output collected traces"

rm ./profile/*.*

sudo bpftrace -e 'uprobe:libc:malloc { @[ustack] = sum(arg0); }' -p $SERVER_PID > ./profile/raw_allocations.out

../FlameGraph/stackcollapse-bpftrace.pl ./profile/raw_allocations.out > ./profile/collapsed_allocations.txt

../FlameGraph/flamegraph.pl --countname=bytes ./profile/collapsed_allocations.txt > ./profile/memory_profile.svg
```

## Generate CPU FlameGraphs

```bash
SERVER_PID=$(pgrep -f ./build/server)

echo "Generating CPU flame graph for PID $SERVER_PID. Put the server under load. Ctrl+C to output collected traces"

rm ./profile/*.*

sudo bpftrace -e 'profile:hz:99 { @[ustack] = count(); }' -p $SERVER_PID > ./profile/cpu_raw.out

sudo ../FlameGraph/stackcollapse-bpftrace.pl ./profile/cpu_raw.out > ./profile/cpu_collapsed.txt

sudo ../FlameGraph/flamegraph.pl ./profile/cpu_collapsed.txt > ./profile/cpu_flamegraph.svg
```
