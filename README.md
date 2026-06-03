# pi-borescope-streamer

A high-performance, asynchronous, zero-allocation MJPEG streaming server designed specifically for Useeplus USB borescope cameras on the Raspberry Pi 5.

This project evolved from a simple synchronous USB-polling script into a robust, multi-client C++ streaming engine. It completely decouples the hardware ingestion thread from the network layer, allowing you to serve real-time video to multiple browser dashboards and VLC clients simultaneously with near-zero CPU overhead.

## 🚀 Features

* **Zero-Heap-Allocation Hot Path:** Built for long-running stability. After initial startup, the video pipeline and streaming multiplexer operate without a single call to `malloc` or `free`, entirely eliminating heap fragmentation and garbage-collection latency.
* **Asynchronous Network Stack:** Powered by `uWebSockets` (an ultra-fast C++ `epoll` engine). A slow client on a bad Wi-Fi connection will never block the hardware camera thread.
* **Zero-Copy Broadcasting:** Video frames are streamed directly from pre-allocated memory pools with hardware-level efficiency, bypassing intermediate deep copies.
* **Multi-Client Support:** Stream to multiple browser tabs or VLC instances simultaneously with aggressive backpressure handling (automatically evicts lagging clients to protect server memory).
* **Client-Side Snapshots:** The web dashboard utilizes the HTML5 `<canvas>` API to capture high-resolution snapshots instantly on the user's device, completely removing image-processing overhead from the Raspberry Pi.

## 🛠️ Architecture Highlights

Most hobbyist MJPEG servers suffer from "buffer churn"—constantly resizing `std::string` or `std::vector` buffers to frame HTTP headers, which eats CPU cycles on embedded hardware.

`pi-borescope-streamer` avoids this by:

1. **Stack-Based Formatting:** HTTP multipart chunk boundaries and payload sizes are calculated entirely on the stack using `std::to_chars` and `std::format_to_n`.
2. **Double-Buffering Hardware IO:** `libusb` ingestion swaps memory blocks atomically, keeping the hardware mutex locked for less than a microsecond.
3. **Strict Memory Layouts:** The memory exchange zone (`SharedFramePipeline`) pre-allocates a fixed pool of memory buffers at startup, constantly recycling them between the USB hardware and network broadcaster to prevent leaks.

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

## 📡 PI Streaming Server Usage

Once the server is running and the camera is plugged in, you can access the streams locally or across your network:

* **Interactive Web Dashboard (with Snapshots):** `http://<raspberry-pi-ip>:8080/`
* **Raw VLC MJPEG Stream:** `http://<raspberry-pi-ip>:8080/stream`

## Run as Video4Linux Daemon

```bash
# Add temporary loopback device. Below we will configure this to run on system boot. This is only to avoid restarting.

sudo rmmod v4l2loopback
sudo modprobe v4l2loopback devices=1 video_nr=7 card_label="Geek szitman supercamera" exclusive_caps=1 max_buffers=8

# Start the v4l2loopback module on boot
echo "v4l2loopback" | sudo tee /etc/modules-load.d/v4l2loopback.conf

# Register the supercamera hardware parameters for the module
echo 'options v4l2loopback devices=1 video_nr=7 card_label="Geek szitman supercamera" exclusive_caps=1' | sudo tee /etc/modprobe.d/v4l2loopback.conf
```

**Ensure you are currently inside the `pi-borescope-streamer` directory before running, as it uses your current path to configure the service.**

```bash
# Create the systemd service file dynamically for your user
cat << EOF | sudo tee /etc/systemd/system/v4l2-borescope@.service > /dev/null
[Unit]
Description=Useeplus Borescope V4L2 Daemon (%i)
After=network.target systemd-modules-load.service
Wants=systemd-modules-load.service

[Service]
Type=simple
User=$USER
Group=$USER
WorkingDirectory=$PWD
ExecStart=$PWD/out/build/release/v4l2-borescope-daemon --dev /dev/%i --width 640 --height 480 --size 131072
Restart=on-failure
RestartSec=5
KillSignal=SIGTERM
TimeoutStopSec=10

[Install]
WantedBy=multi-user.target
EOF
```

```bash
# Refresh systemd, enable the service, and start it
sudo systemctl daemon-reload
sudo systemctl enable v4l2-borescope@video7.service
sudo systemctl start v4l2-borescope@video7.service

# Verify the service is running successfully
systemctl status v4l2-borescope@video7.service
```

```bash
# Grab a snapshot of a frame from the camera and save it as `snapshot.jpg` in the current directory
ffmpeg -f v4l2 -i /dev/video7 -vframes 1 -update 1 snapshot.jpg
```

```bash
# Open the cameras video stream using VLC Media Player, if you are using Raspberry Pi Desktop
vlc v4l2:///dev/video7
```

### Using uStreamer

Here is how to deploy uStreamer to create a high-performance HTTP MJPEG streaming server for your Useeplus USB camera.

**Compile and Install uStreamer**
First, install the required dependencies and build the application from source:

```bash
sudo apt update
sudo apt install libevent-dev libjpeg-dev libbsd-dev

git clone --depth=1 https://github.com/pikvm/ustreamer.git
cd ustreamer
make
sudo make install

```

**Start the v4l2-borescope Daemon**
Ensure your custom V4L2 loopback device is active. This daemon bridges the proprietary Useeplus protocol into a standard video feed that uStreamer can natively consume.
*(See the Daemon Configuration section above for setup instructions).*

**Launch the uStreamer Server**
Start the MJPEG HTTP server, pointing it to your virtual video node (`/dev/video7`). Binding the host to `0.0.0.0` ensures the stream is accessible from any device on your local network:

```bash
ustreamer -d /dev/video7 -r 640x480 -f 30 -p 8080 --host 0.0.0.0

```

**Viewing the Stream**
Once the uStreamer server is running and the camera is plugged in, you can access the streams locally or across your network:

* **Video Stream** `http://<raspberry-pi-ip>:8080/stream`
* **Capture Snapshot** `http://<raspberry-pi-ip>:8080/snapshot`
* **uStreamer Dashboard** `http://<raspberry-pi-ip>:8080/`


### Quick Testing with FFmpeg

For quick debugging or simple tests without installing a dedicated web server, you can use FFmpeg's built-in HTTP listener to broadcast the stream. 

*(Note: While highly convenient, FFmpeg's internal HTTP server is single-threaded and less resilient to network drops than uStreamer. Use this primarily for local testing).*

```bash
ffmpeg -f v4l2 -i /dev/video7 -c:v copy -f mpjpeg -listen 4 http://0.0.0.0:8080
```

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

Want to look under the hood? I have written a detailed guide on how to configure a Raspberry Pi kernel for eBPF and 
generate CPU/Memory FlameGraphs to prove the zero-allocation architecture.

* [Profiling using BPF and FlameGraphs](PerformanceProfiling.md)
* [Useeplus Protocol Deep Dive](UseeplusProtocol.md) - Hex dumps, hardware quirks, and frame assembly math.

## 🧠 Acknowledgements

This project was heavily inspired by the reverse-engineering work of 
[hbens](https://github.com/hbens/geek-szitman-supercamera), [jmz3](https://github.com/jmz3/EndoscopeCamera), and 
[doctormo](https://github.com/doctormo). Their original proofs-of-concept laid the groundwork for decoding the 
`com.useeplus.protocol`.

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

For information regarding the third-party libraries used in this project, please see the 
[THIRD_PARTY_LICENSES](THIRD_PARTY_LICENSES.md) file.