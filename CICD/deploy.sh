#!/bin/bash
set -e

# 1. Build the binary inside your Ubuntu Docker environment on your Mac
echo "🚀 Building Linux ARM64 binary inside Docker..."
docker build --target builder -t borescope-builder .

# 2. Extract the compiled binary out of the Docker container layers
echo "📦 Extracting binary..."
docker run --rm --entrypoint cat borescope-builder /app/out/build/debug/pi-borescope-streamer > ./pi-borescope-streamer
chmod +x ./pi-borescope-streamer

# 3. Securely copy the binary over your local network to your Raspberry Pi
echo "🚚 Shipping binary to Raspberry Pi..."
# Replace 'pi' with your user and 'raspberrypi.local' with your Pi's IP address
scp ./pi-borescope-streamer pi@raspberrypi.local:/home/pi/pi-borescope-streamer

# 4. Cleanup the local temporary binary file
rm ./pi-borescope-streamer

echo "✅ Done! Run it on your Pi via: ./pi-borescope-streamer 8080"
