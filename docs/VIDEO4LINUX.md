### Testing `useeplus v4l2` Driver

Once the driver is installed, you can use ffmpeg to take a snapshot of the video feed.

```bash
# Grab a snapshot of a frame from the camera and save it as `snapshot.jpg` in the current directory.
# Replace /dev/video0 with the device path to your camera.
ffmpeg -f v4l2 -i /dev/vide0 -vframes 1 -update 1 snapshot.jpg

```

### Using uStreamer

Here is how to deploy [uStreamer](https://github.com/pikvm/ustreamer) to create a high-performance HTTP MJPEG streaming
server for your Useeplus USB camera.

**Compile and Install uStreamer**
First, install the required dependencies and build the application from source:

```bash
sudo apt update
sudo apt install libevent-dev libjpeg-dev libbsd-dev

git clone --depth=1 [https://github.com/pikvm/ustreamer.git](https://github.com/pikvm/ustreamer.git)
cd ustreamer
make
sudo make install

```

#### Launch the uStreamer Server

Start the MJPEG HTTP server, pointing it to your virtual video node (`/dev/video0`). Binding the
host to `0.0.0.0` ensures the stream is accessible from any device on your local network:

```bash
ustreamer -d /dev/video0 -r 640x480 -f 30 -m MJPEG -p 8080 --host 0.0.0.0

```

#### Viewing the Stream

Once the uStreamer server is running and the camera is plugged in, you can access the streams
locally or across your network:

- **Video Stream** `http://<raspberry-pi-ip>:8080/stream`
- **Capture Snapshot** `http://<raspberry-pi-ip>:8080/snapshot`
- **uStreamer Dashboard** `http://<raspberry-pi-ip>:8080/`
