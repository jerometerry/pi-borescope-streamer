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
* **Dependencies:**
  * `libusb-1.0-0-dev`
  * `cmake` (Version 3.10.0 or newer)
  * `ninja-build` (Highly recommended generator backend)
  * `pkg-config`
  * C++23 compliant compiler (`gcc` 13+ or `clang` 17+)

```bash
sudo apt update
sudo apt install libusb-1.0-0-dev cmake ninja-build pkg-config build-essential
```

## ⚙️ Build and Run

### 1. Clone the repository
```bash
git clone https://github.com/jerometerry/pi-borescope-streamer.git
cd pi-borescope-streamer
```

### 2. Compile via CMake Presets (Recommended)
The project includes predefined CMake presets to quickly switch profiles.

**For local development (Fast compiling with debug symbols):**
```bash
cmake . --preset debug
cmake --build --preset debug
```

**For building docs**
```bash
cmake . --preset debug
cmake --build --preset docs-debug
```

**For running tests / generating code coverage report**
```bash
cmake . --preset debug -DENABLE_COVERAGE=ON --fresh
cmake --build --preset debug --target run_project_tests
ctest --test-dir out/build/debug
mkdir -p coverage && gcovr -r . --object-directory out/build/debug --filter "src/.*" --html-details coverage/index.html
```

**For production deployment (Optimized with -O2, LTO, and Pi 5 Cortex-A76 core tuning):**
```bash
cmake . --preset release
cmake --build --preset release
```

### 3. Alternative: Manual Compile via CMake CLI
If you want to configure build directories manually without using presets:
```bash
cmake -B out/build/default -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/default
```

### 4. Launch the server
Run the binary out of its target profile directory. You can optionally specify a custom port (default is `8080`).
```bash
./out/build/release/pi-borescope-streamer
```

*Note: For a clean rebuild, run `cmake --build --preset clean-debug` or `cmake --build --preset clean-release` to clear old artifacts.*

## 📡 MJPEG Streaming Server Usage

Once the server is running and the camera is plugged in, you can access the streams locally or across your network:

* **Interactive Web Dashboard:** `http://<raspberry-pi-ip>:8080/web`
* **Raw VLC MJPEG Stream:** `http://<raspberry-pi-ip>:8080/`
* **Latest Snapshot (Triggered by Hardware Button):** `http://<raspberry-pi-ip>:8080/snapshot`

## 🐳 Docker Build

A Docker multi-stage environment is available to run and test the build process in an isolated Linux container using local source assets.

### Build the Image
To build the image natively using the optimized production profile preset:
```bash
docker build -t pi-borescope-test .
```

### Run the Container
1. Plug the camera into a USB port on your Docker host hardware (e.g., Raspberry Pi).
2. Mount the USB bus path using the `--device` runtime argument:

```bash
docker run --device=/dev/bus/usb -p 8080:8080 pi-borescope-test
```

## 🔬 Advanced Documentation

Want to look under the hood? I have written a detailed guide on how to configure a Raspberry Pi kernel for eBPF and generate CPU/Memory FlameGraphs to prove the zero-allocation architecture.

* [Profiling using BPF and FlameGraphs](PerformanceProfiling.md)

## 🧠 Acknowledgements

This project was heavily inspired by the reverse-engineering work of [hbens](https://github.com/hbens/geek-szitman-supercamera), [jmz3](https://github.com/jmz3/EndoscopeCamera), and [doctormo](https://github.com/doctormo). Their original proofs-of-concept laid the groundwork for decoding the `com.useeplus.protocol`.

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
