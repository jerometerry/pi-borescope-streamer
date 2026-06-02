FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

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
    zlib1g \
    zlib1g-dev \
    openssl \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . .

RUN cmake . --preset debug \
    && cmake --build --preset debug

EXPOSE 8080

CMD ["./out/build/debug/pi-borescope-streamer", "8080"]
