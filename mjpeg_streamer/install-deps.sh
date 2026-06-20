#!/usr/bin/env bash
set -e

mkdir -p third_party

if [ ! -d "third_party/uWebSockets" ]; then
    echo "Cloning uWebSockets..."
    git clone --depth 1 --branch v20.67.0 https://github.com/uNetworking/uWebSockets.git third_party/uWebSockets
else
    echo "uWebSockets already exists, skipping clone."
fi

if [ ! -d "third_party/uSockets" ]; then
    echo "Cloning uSockets..."
    git clone --depth 1 --branch v0.8.8 https://github.com/uNetworking/uSockets.git third_party/uSockets
else
    echo "uSockets already exists, skipping clone."
fi

echo "Building uSockets with Zig..."
cd third_party/uSockets
make clean
make CC="zig cc" CXX="zig c++" AR="zig ar" WITH_LTO=0
cd ../../

echo "Dependencies successfully configured!"
