# pi-borescope-streamer

A high-performance, asynchronous, zero-allocation MJPEG streaming server designed specifically for Useeplus USB borescope cameras on the Raspberry Pi 5.

This project evolved from a simple synchronous USB-polling script into a robust, multi-client C++ streaming engine. It completely decouples the hardware ingestion thread from the network layer, allowing you to serve real-time video to multiple browser dashboards and VLC clients simultaneously with near-zero CPU overhead.

## 🚀 Features

* **Zero-Heap-Allocation Hot Path:** Built for long-running stability. After initial startup, the event loop and streaming multiplexer operate without a single call to `malloc` or `free`, entirely eliminating heap fragmentation and garbage-collection latency.
* **Asynchronous Network Stack:** Uses a custom `poll`-based event loop with non-blocking sockets. A slow client on a bad Wi-Fi connection will never block the hardware camera thread.
* **Zero-Copy Broadcasting:** Video frames are written directly to pre-allocated client outboxes with hardware-level `memcpy`, bypassing intermediate deep copies.
* **Multi-Client Support:** Stream to multiple browser tabs or VLC instances simultaneously with graceful backpressure handling (automatically drops frames for clients whose buffers exceed 2MB).
* **Hardware Button Integration:** Fully supports the physical button on Useeplus borescopes to trigger high-resolution still-frame captures without interrupting the MJPEG stream.

## 🛠️ Architecture Highlights

Most hobbyist MJPEG servers suffer from "buffer churn"—constantly resizing `std::string` or `std::vector` buffers to frame HTTP headers, which eats CPU cycles on embedded hardware.

`pi-borescope-streamer` avoids this by:

1. **Stack-Based Formatting:** HTTP multipart chunk boundaries and payload sizes are calculated entirely on the stack using `std::to_chars` and `std::format_to_n`.
2. **Double-Buffering Hardware IO:** `libusb` ingestion swaps memory blocks atomically, keeping the hardware mutex locked for less than a microsecond.
3. **Strict Memory Layouts:** Vectors for active frames, snapshots, and client buffers are pre-allocated (`reserve()`) at startup to their maximum safe sizes.

## 📦 Prerequisites

* **Hardware:** Raspberry Pi (Optimized for Pi 5, but runs on Pi 4/3)
* **Camera:** USB Borescope/Endoscope (Tested with Useeplus Hardware Rev 1.00 - VID: `0x2ce3`, `0x0329`)
* **OS:** Raspberry Pi OS (Debian Bookworm or newer)
* **Dependencies:** * `libusb-1.0-0-dev`
  * C++20 compliant compiler (`gcc` 10+)



```bash
sudo apt update
sudo apt install libusb-1.0-0-dev build-essential

```

## ⚙️ Build and Run

1. **Clone the repository:**

```bash
git clone https://github.com/jerometerry/pi-borescope-streamer.git
cd pi-borescope-streamer

```

2. **Compile the server:**
*(Assuming you are using a standard Make/CMake setup, otherwise insert your build command here)*

```bash
make

```

3. **Launch the server:**
You can optionally specify a custom port (default is `8080`).

```bash
./build/server 9000

```

## Docker Build

I use `docker build` as a quick sanity check that this will compile on other machines. 

The server won't start if there is no USB camera connected. I haven't investigated how to make that work in Docker.

```
docker build --build-arg CACHEBUST=$(date +%s) -t pi-borescope-test .
```

## Profiling using BBF and FlameGraphs

Finding performance bottlenecks in C++ code is not as complicated as you might think, using modern tools such as BPF and FlameGraphs. The cost complicated part is configuring the Linux kernel to enable BTF Type Info and uprobes. This may involve recompiling the kernel - for example on a Raspberry PI with the default OS install.

### Linux Kernel Configs

If you want to use eBPF, for example to generate FlameGraphs, you'll need to enable BTF (BPF Type Format) Support. Here's the options that need to be configured in Raspberry Pi kernel configs when re-compiling, if you are using `make menuconfig` to generate the .config file. 

- Enable "Kernel hacking -> Compile-time checks and compiler options -> Compile the Kernel with debug info"
  - Select "Rely on the toolchain's implicit default DWARF version"
- Enable "Kernel hacking -> Compile-time checks and compiler options -> Generate BTF typeinfo"
- Enable "Kernel hacking -> Tracers -> Enable uprobes-based dynamic events"

### Compiler Flags

If you want to run BPF / linux perf on custom applications you are building, ensure to add the following compiler flags:
-  `-g`:  Embeds your source code maps into the build
- `-fno-omit-frame-pointer`:  Instructs the compiler to keep the frame pointer register on the stack for every function call.

Here's an example of `CXXFLAGS` from a Makefile with these compiler options specified

```
CXXFLAGS := -std=c++23 -Wall -Wextra -O2 -I$(BUILD_DIR) -mcpu=cortex-a76 -mtune=cortex-a76 -flto=auto -g -fno-omit-frame-pointer
```

### Clone Brendan Gregg's FlameGraph Repo

```
git clone https://github.com/brendangregg/FlameGraph.git
```

### Install bpftrace

```
sudo apt update
sudo apt install bpftrace
```

### Install linux-perf

```
sudo apt update
sudo apt install linux-perf
```

### Generate Memory Allocation FlameGraph

```bash
SERVER_PID=$(pgrep -f ./build/server)

echo "Generating memory allocation flame graph for PID $SERVER_PID. Put the server under load. Ctrl+C to output collected traces"

rm ./profile/*.*

sudo bpftrace -e 'uprobe:libc:malloc { @[ustack] = sum(arg0); }' -p $SERVER_PID > ./profile/raw_allocations.out

../FlameGraph/stackcollapse-bpftrace.pl ./profile/raw_allocations.out > ./profile/collapsed_allocations.txt

../FlameGraph/flamegraph.pl --countname=bytes ./profile/collapsed_allocations.txt > ./profile/memory_profile.svg
```

### Generate CPU FlameGraphs

```bash
SERVER_PID=$(pgrep -f ./build/server)

echo "Generating CPU flame graph for PID $SERVER_PID. Put the server under load. Ctrl+C to output collected traces"

rm ./profile/*.*

sudo bpftrace -e 'profile:hz:99 { @[ustack] = count(); }' -p $SERVER_PID > ./profile/cpu_raw.out

sudo ../FlameGraph/stackcollapse-bpftrace.pl ./profile/cpu_raw.out > ./profile/cpu_collapsed.txt

sudo ../FlameGraph/flamegraph.pl ./profile/cpu_collapsed.txt > ./profile/cpu_flamegraph.svg
```

## 📡 MJPEG Streaming Server Usage

Once the server is running and the camera is plugged in, you can access the streams locally or across your network:

* **Interactive Web Dashboard:** `http://<raspberry-pi-ip>:9000/web`
* **Raw VLC MJPEG Stream:** `http://<raspberry-pi-ip>:9000/`
* **Latest Snapshot (Triggered by Hardware Button):** `http://<raspberry-pi-ip>:9000/snapshot`

## 🧠 Acknowledgements

This project was heavily inspired by the reverse-engineering work of [hbens](https://github.com/hbens/geek-szitman-supercamera), [jmz3](https://github.com/jmz3/EndoscopeCamera), and [doctormo](https://github.com/doctormo). Their original proofs-of-concept laid the groundwork for decoding the `com.useeplus.protocol`.

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
