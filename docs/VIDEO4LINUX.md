## Run as Video4Linux Daemon

If you want to use supercameras with tools such as Video4Linux or ffmpeg, instead of using the `pi-borescope-streamer`
you can use `v4l2-borescope-daemon` to setup a virtual v4l2 device. This is a little more involved than simply running
a web server, but still manageable.

### Create V42L Virtual Device

```bash
# remove v4l2loopback module from the kernel, so we can configure a virtual device
sudo rmmod v4l2loopback

# Add a virtual v42l device. Below we will configure this to run on system boot. This is only to avoid restarting.
sudo modprobe v4l2loopback devices=1 video_nr=7 card_label="Geek szitman supercamera" exclusive_caps=1 max_buffers=8

# Start the v4l2loopback module on boot
echo "v4l2loopback" | sudo tee /etc/modules-load.d/v4l2loopback.conf

# Register the supercamera hardware parameters for the module
echo 'options v4l2loopback devices=1 video_nr=7 card_label="Geek szitman supercamera" exclusive_caps=1' | sudo tee /etc/modprobe.d/v4l2loopback.conf

```

### Create SystemD Daemon Service Configuration

This will create a new daemon service for the `v4l2-borescope-daemon` application.

**Ensure you are currently inside the `pi-borescope-streamer` directory before running, as it uses your current path to configure the service.**

```bash
# Create the systemd service file dynamically for your user
# the @ symbol here is a placeholder for a variable that will be mapped to %i below, allowing us to run multiple 
# daemons for multiple connected camera scenarios
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

### Create and Start `v4l2-borescope-daemon` Daemon

```bash
# Refresh systemd, enable the service, and start it
sudo systemctl daemon-reload

# This will create a daemon for the video7 device. Replace video7 with the name of the camera you want to use. 
sudo systemctl enable v4l2-borescope@video7.service

# Start the daemon for the the video7 device. Replace video7 with the name of the camera you want to use. 
sudo systemctl start v4l2-borescope@video7.service

# Verify the service is running successfully. Replace video7 with the name of the camera you want to use. 
systemctl status v4l2-borescope@video7.service

```

### Stop `v4l2-borescope-daemon` Daemon

```bash
sudo systemctl stop v4l2-borescope@video7.service

```

### Testing `v4l2-borescope-daemon` Daemon

Once the daemon is running, you can use ffmpeg to take a snapshot of the video feed.

```bash
# Grab a snapshot of a frame from the camera and save it as `snapshot.jpg` in the current directory
ffmpeg -f v4l2 -i /dev/video7 -vframes 1 -update 1 snapshot.jpg

```

For quick debugging or simple tests without installing a dedicated web server, you can use FFmpeg's built-in HTTP
listener to broadcast the stream.

*(Note: While highly convenient, FFmpeg's internal HTTP server is single-threaded and less resilient to network drops
than uStreamer. Use this primarily for local testing).*

```bash
ffmpeg -f v4l2 -i /dev/video7 -c:v copy -f mpjpeg -listen 4 [http://0.0.0.0:8080](http://0.0.0.0:8080)

```

### Viewing the V4L2 Video Stream

If you are using the Raspberry PI Desktop, you can view the video feed with VLC

```bash
# Open the cameras video stream using VLC Media Player, if you are using Raspberry Pi Desktop
vlc v4l2:///dev/video7

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

#### Start the v4l2-borescope Daemon

Ensure your custom V4L2 loopback device is active. This daemon bridges the proprietary Useeplus protocol into a standard video feed that uStreamer can natively consume.
*(See the Daemon Configuration section above for setup instructions).*

#### Launch the uStreamer Server

Start the MJPEG HTTP server, pointing it to your virtual video node (`/dev/video7`). Binding the host to `0.0.0.0` ensures the stream is accessible from any device on your local network:

```bash
ustreamer -d /dev/video7 -r 640x480 -f 30 -p 8080 --host 0.0.0.0


```

#### Viewing the Stream

Once the uStreamer server is running and the camera is plugged in, you can access the streams locally or across your network:

* **Video Stream** `http://<raspberry-pi-ip>:8080/stream`
* **Capture Snapshot** `http://<raspberry-pi-ip>:8080/snapshot`
* **uStreamer Dashboard** `http://<raspberry-pi-ip>:8080/`