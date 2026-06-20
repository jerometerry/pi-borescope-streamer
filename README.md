# pi-borescope-streamer

A high-performance, asynchronous, zero-allocation MJPEG streaming server designed specifically for Useeplus USB borescope cameras on the Raspberry Pi 5.

This project evolved from a simple synchronous USB-polling script into a robust, multi-client C++ streaming engine. It completely decouples the hardware ingestion thread from the network layer, allowing you to serve real-time video to multiple browser dashboards and VLC clients simultaneously with near-zero CPU overhead.

## 🚀 Features

- **Zero User-Space Memory Churn:** Built for long-running stability. The C++ video pipeline, HTTP header generation, and `uWebSockets` streaming multiplexer operate completely free of dynamic memory allocation. By pre-allocating persistent memory pools and utilizing zero-copy `std::span` views of Video Frame Fragments, the architecture completely eliminates user-space heap fragmentation. _(Note: The pipeline is so heavily optimized that eBPF profiling reveals the only remaining allocations on the entire hot path are the unavoidable `calloc` calls triggered by the underlying Linux `usbfs` kernel driver wrapping hardware URBs)._
- **Asynchronous Network Stack:** Powered by `uWebSockets` (an ultra-fast C++ `epoll`/`kqueue` engine). A slow client on a bad Wi-Fi connection will never block the hardware camera thread.
- **Zero-Copy Broadcasting:** Complete Presentation-Layer Video Frames are streamed directly from pre-allocated memory pools with hardware-level efficiency, bypassing intermediate deep copies.
- **Multi-Client Support:** Stream to multiple browser tabs or VLC instances simultaneously with aggressive backpressure handling (automatically evicts lagging clients to protect server memory).
- **Client-Side Snapshots:** The web dashboard utilizes the HTML5 `<canvas>` API to capture high-resolution snapshots instantly on the user's device, completely removing image-processing overhead from the Raspberry Pi.

## 🛠️ Architecture Highlights

Most hobbyist MJPEG servers suffer from "buffer churn"—constantly resizing `std::string` or `std::vector` buffers to concatenate USB Frames or build HTTP headers, which eats CPU cycles on embedded hardware.

`pi-borescope-streamer` avoids this by:

1. **Stack-Based Formatting:** HTTP multipart chunk boundaries and payload sizes are calculated entirely on the stack using `std::to_chars` and `std::format_to_n`.
2. **Double-Buffering Hardware IO:** `libusb` ingestion swaps memory blocks atomically, keeping the hardware mutex locked for less than a microsecond.
3. **Strict Memory Layouts:** The memory exchange zone utilizes an **LMAX Disruptor** ring buffer to pre-allocate a fixed pool of memory buffers at startup, constantly recycling them between the USB hardware and the network broadcaster to prevent leaks.

## 📦 Prerequisites

- **Hardware:** Raspberry Pi (Optimized for Pi 5, but runs on Pi 4/3) or Apple Silicon Mac.
- **Camera:** USB Borescope/Endoscope (Tested with Useeplus Hardware Rev 1.00 - VID: `0x2ce3`, `0x0329`)
- **OS:** Raspberry Pi OS (Debian Bookworm or newer) or macOS.
- **Dependencies:**
- `libusb-1.0-0-dev` (Linux) or `libusb` via Homebrew (macOS)
- `openssl`
- `make`
- `zig` (Used as a drop-in cross-platform C/C++ compiler)
- `pkg-config`

**Linux Setup:**

```bash
sudo apt update
sudo apt install libusb-1.0-0-dev make pkg-config libssl-dev
# Install Zig via official binaries or your preferred package manager

```

**macOS Setup:**

```bash
brew install libusb openssl@3 zig pkg-config

```

## ⚙️ Build and Run Pi Borescope Streamer

Depending on your host operating system, the `Makefile` will automatically detect your platform and build the appropriate binaries into the `build/` directory:

- **mjpeg_server** (macOS): MJPEG HTTP Web Server for streaming the video feed via `libusb`.
- **v4l2_mjpeg_server** (Linux): MJPEG HTTP Web Server natively integrated with Video4Linux2.
- **mjpeg_stream_capture** (macOS): Console application for saving the video feed in raw binary format.
- **mjpeg_frame_extractor** (macOS): Console application that can extract and save individual JPEG Video Frames from the binary capture.

### 1. Clone the repository

```bash
git clone https://github.com/jerometerry/pi-borescope-streamer.git
cd pi-borescope-streamer

```

### 2. Compile and Test

The build system will automatically download and compile third-party dependencies (`uWebSockets`, `uSockets`, `googletest`) into an isolated `build/` environment before compiling the core project.

```bash
# Build the core executables
make

# Run the project and Linux driver test suites
make test

```

> **Note:** For comprehensive development instructions—including running static analysis, generating documentation, and utilizing the `./run-format.sh` script—please refer to the **[Build Guide (BUILD.md)](https://www.google.com/search?q=docs/BUILD.md)**.

### 3. Launch Pi Borescope Streamer

The primary application is a small web server designed to stream the camera feed to web browsers.

Run the compiled binary out of the `build` directory.

**On Linux:**

```bash
./build/v4l2_mjpeg_server

```

**On macOS:**

```bash
./build/mjpeg_server

```

Once the camera is plugged in and the server is running, you can access the streams locally or across your network:

- **Interactive Web Dashboard (with Snapshots):** `http://<server-ip>:18080/`
- **Raw VLC MJPEG Stream:** `http://<server-ip>:18080/stream`

## 🔬 Advanced Documentation

Want to look under the hood? I have written detailed guides outlining the protocol, memory profiling, and deployment strategies:

- [Running as a Video4Linux Daemon](https://www.google.com/search?q=docs/VIDEO4LINUX.md)
- [Running in Docker](https://www.google.com/search?q=docs/DOCKER.md)
- [Profiling using BPF and FlameGraphs](https://www.google.com/search?q=docs/PerformanceProfiling.md) - Prove the zero-allocation architecture on your own hardware.
- [Useeplus Protocol Deep Dive](https://www.google.com/search?q=docs/UseeplusProtocol.md) - OSI-aligned hex dumps, hardware quirks, and the math behind reconstructing presentation-layer Video Frames from raw Link-Layer USB Frames.

## 🧠 Acknowledgements

This project was heavily inspired by the reverse-engineering work of
[hbens](https://github.com/hbens/geek-szitman-supercamera), [jmz3](https://github.com/jmz3/EndoscopeCamera), and
[doctormo](https://github.com/doctormo). Their original proofs-of-concept laid the groundwork for decoding the
`com.useeplus.protocol`.

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](https://www.google.com/search?q=LICENSE) file for details.

For information regarding the third-party libraries used in this project, please see the
[THIRD_PARTY_LICENSES](https://www.google.com/search?q=THIRD_PARTY_LICENSES.md) file.
