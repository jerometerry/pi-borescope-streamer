#!/usr/bin/env bash
set -e

USEEPLUS_DIR="build/useeplus"

if [ ! -d "$USEEPLUS_DIR" ]; then
    echo "Cloning useeplus..."
    git clone -c advice.detachedHead=false --branch main https://github.com/jerometerry/useeplus.git "$USEEPLUS_DIR"
fi

echo "Building useeplus..."
cd "$USEEPLUS_DIR"
make clean
make
