#!/usr/bin/env bash
set -e

# Establish the new isolated path inside the build directory
SOCKETS_DIR="build/sockets"
mkdir -p "$SOCKETS_DIR"

# Clone uWebSockets if it doesn't exist
if [ ! -d "$SOCKETS_DIR/uWebSockets" ]; then
    echo "Cloning uWebSockets into build directory..."
    git clone --depth 1 --branch v20.67.0 https://github.com/uNetworking/uWebSockets.git "$SOCKETS_DIR/uWebSockets"
else
    echo "uWebSockets already exists, skipping clone."
fi

# Clone uSockets if it doesn't exist
if [ ! -d "$SOCKETS_DIR/uSockets" ]; then
    echo "Cloning uSockets into build directory..."
    git clone --depth 1 --branch v0.8.8 https://github.com/uNetworking/uSockets.git "$SOCKETS_DIR/uSockets"
else
    echo "uSockets already exists, skipping clone."
fi

# Clean and rebuild uSockets using Zig inside the build directory
echo "Building uSockets with Zig..."
cd "$SOCKETS_DIR/uSockets"
make clean
make CC="zig cc" CXX="zig c++" AR="zig ar" WITH_LTO=0
cd ../../..

echo "Dependencies successfully configured inside build directory!"
