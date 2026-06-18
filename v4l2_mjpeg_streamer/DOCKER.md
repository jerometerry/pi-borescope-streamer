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