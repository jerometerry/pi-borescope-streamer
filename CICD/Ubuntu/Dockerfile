# --- Stage 1: Build Environment ---
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Install build tools and dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    git \
    cmake \
    ninja-build \
    pkg-config \
    clang-tidy \
    cppcheck \
    iwyu \
    libusb-1.0-0-dev \
    zlib1g-dev \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy configuration files first to preserve layer cache for dependencies
COPY CMakeLists.txt CMakePresets.json ./
# If you have a source directory, copy its structure or use a dummy file to configure
# Or simply copy everything if you don't use heavy external package managers
COPY . .

# Configure and Build
RUN cmake --preset debug \
    && cmake --build --preset debug

# --- Stage 2: Runtime Environment (Lean & Secure for Raspberry Pi) ---
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

# Install ONLY runtime shared libraries (no compilers, no -dev packages)
RUN apt-get update && apt-get install -y \
    libusb-1.0-0 \
    zlib1g \
    openssl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy only the compiled binary from the builder stage
COPY --from=builder /app/out/build/debug/pi-borescope-streamer ./pi-borescope-streamer

EXPOSE 8080

ENTRYPOINT ["./pi-borescope-streamer"]
CMD ["8080"]
