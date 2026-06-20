# Testing the Video4Linux Driver & Benchmarking

Once the `useeplus_v4l2` kernel module is loaded and the hardware is registered to a video node (e.g., `/dev/video0`), you can validate the stream using standard Linux video utilities.

## 1. Capturing Snapshots with FFmpeg

You can use `ffmpeg` to interact directly with the V4L2 subsystem and extract a complete, Presentation-Layer Video Frame to your local disk. This is the fastest way to verify that the kernel driver is successfully reconstructing the MJPEG payload.

```bash
# Grab a snapshot of a single Video Frame from the camera and save it as `snapshot.jpg`.
# Note: Replace /dev/video0 with the actual device path assigned to your camera.
ffmpeg -f v4l2 -i /dev/video0 -vframes 1 -update 1 snapshot.jpg

```

---

## 2. Benchmarking with uStreamer

While this repository provides its own highly optimized, zero-allocation streaming server (`v4l2_mjpeg_server`), [uStreamer](https://github.com/pikvm/ustreamer) is an excellent, battle-tested third-party tool. It is highly recommended to use it for validating your kernel driver's stability or establishing baseline CPU and memory performance metrics to compare against your custom server.

### Compile and Install uStreamer

First, install the required dependencies and build the application from source:

```bash
sudo apt update
sudo apt install libevent-dev libjpeg-dev libbsd-dev

git clone --depth=1 https://github.com/pikvm/ustreamer.git
cd ustreamer
make
sudo make install

```

### Launch the uStreamer Server

Start the MJPEG HTTP server, pointing it to your virtual video node (`/dev/video0`). Binding the host to `0.0.0.0` ensures the stream is accessible from any device on your local network:

```bash
ustreamer -d /dev/video0 -r 640x480 -f 30 -m MJPEG -p 8080 --host 0.0.0.0

```

### Viewing the Stream

Once the `uStreamer` server is running and the camera is capturing Video Frames, you can access the stream locally or across your network:

- **Video Stream:** `http://<server-ip>:8080/stream`
- **Capture Snapshot:** `http://<server-ip>:8080/snapshot`
- **uStreamer Dashboard:** `http://<server-ip>:8080/`
