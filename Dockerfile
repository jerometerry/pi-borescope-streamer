FROM ubuntu:24.04

# Prevent apt from prompting for user input during installation
ENV DEBIAN_FRONTEND=noninteractive

# 1. Install all required dependencies
# We include git and ca-certificates to securely clone the repository
RUN apt-get update && apt-get install -y \
    build-essential \
    make \
    libusb-1.0-0-dev \
    git \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# 2. Set the working directory inside the container
WORKDIR /app

# 3. Clone the repository
# The CACHEBUST argument allows you to force Docker to pull the latest code 
# instead of using a cached layer from a previous git clone.
ARG CACHEBUST=1
RUN git clone https://github.com/jerometerry/pi-borescope-streamer.git 

WORKDIR /app/pi-borescope-streamer
RUN make
# 5. Expose the default network port
EXPOSE 8080

# 6. Define the command to run when the container starts
CMD ["./build/server", "8080"]
