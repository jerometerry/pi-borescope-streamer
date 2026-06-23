#!/usr/bin/env bash
set -e

THIRD_PARTY_DIR="third_party"
mkdir -p "$THIRD_PARTY_DIR"

if [ ! -d "$THIRD_PARTY_DIR/useeplus" ]; then
    echo "Cloning useeplus library..."
    git clone https://github.com/jerometerry/useeplus.git "$THIRD_PARTY_DIR/useeplus"
fi
