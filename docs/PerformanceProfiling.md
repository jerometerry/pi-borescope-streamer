# Profiling using BPF and FlameGraphs

Finding performance bottlenecks in C++ code is not as complicated as you might think when utilizing modern observability tools like BPF (Berkeley Packet Filter) and FlameGraphs. These tools allow you to peer into the kernel and user-space simultaneously with zero instrumentation overhead in your source code.

This document serves as a playbook for generating CPU and Memory FlameGraphs, and tracking latency across the network, event loop, and USB hardware boundaries on a Raspberry Pi.

## 1. Environment Setup

### Install User-Space Tooling

You will need `bpftrace`, `linux-perf`, and Brendan Gregg's official FlameGraph scripts to capture and visualize the data.

```bash
sudo apt update
sudo apt install bpftrace linux-perf

# Clone the FlameGraph repository into your workspace
git clone https://github.com/brendangregg/FlameGraph.git

```

### Linux Kernel Configs (BTF Support)

To use eBPF effectively (especially for tracking kernel structures and uprobes), your Linux kernel must be compiled with BTF (BPF Type Format) support. On a standard Raspberry Pi OS install, this may require recompiling the kernel. _(See the [Recompiling Kernel](https://www.google.com/search?q=../RecompileKernel.md) docs for a step-by-step guide)._

If you are using `make menuconfig` to generate your `.config` file, ensure the following options are enabled:

- **Enable:** `Kernel hacking -> Compile-time checks and compiler options -> Compile the Kernel with debug info`
- _Select:_ `Rely on the toolchain's implicit default DWARF version`

- **Enable:** `Kernel hacking -> Compile-time checks and compiler options -> Generate BTF typeinfo`
- **Enable:** `Kernel hacking -> Tracers -> Enable uprobes-based dynamic events`

### (Optional) BCC Tools Installation

For advanced BPF scripting capabilities, you can build the BCC `libbpf-tools` suite.

```bash
# Clone the BCC repository
git clone --recurse-submodules https://github.com/iovisor/bcc.git
cd bcc/libbpf-tools

# Note: On Raspberry Pi, you may need to symlink headers or llvm-strip first:
# sudo ln -sf /usr/include/aarch64-linux-gnu/asm /usr/include/asm
# sudo ln -s /usr/bin/llvm-strip-19 /usr/bin/llvm-strip

# Build the toolkit
make

```

---

## 2. Preparing the Application for Profiling

If you want to run BPF or `linux-perf` on a custom application, you must instruct the compiler to preserve the debugging symbols and stack frames.

Our `Makefile` automatically injects the necessary flags for Linux targets:

- `-g`: Embeds your source code symbol maps into the binary.
- `-fno-omit-frame-pointer`: Instructs the compiler to keep the frame pointer register on the stack for every function call, allowing BPF to perform deep stack walks.

```makefile
# From the project Makefile
CXXFLAGS := -std=c++2b -Wall -Wextra -g -fno-omit-frame-pointer -Wno-deprecated-declarations
CFLAGS   := -Wall -Wextra -g -fno-omit-frame-pointer

```

To run a production-ready profile, simply build the project normally. The Zig compiler handles the optimizations natively:

```bash
make clean
make

```

---

## 3. Generating FlameGraphs (Macro-Profiling)

FlameGraphs provide a high-level visual map of where your application is spending its time or memory.

_Note: On Linux, our build system generates the `v4l2_mjpeg_server` binary. Ensure the server is running and actively streaming to a client before executing these scripts._

### Memory Allocation FlameGraph

Prove the zero-allocation architecture by tracing all `malloc` calls across the system.

```bash
SERVER_PID=$(pgrep -f ./build/v4l2_mjpeg_server)
mkdir -p ./profile
rm -f ./profile/*.*

echo "Generating memory allocation flame graph for PID $SERVER_PID. Put the server under load. Press Ctrl+C to output collected traces."

sudo bpftrace -e 'uprobe:libc:malloc { @[ustack] = sum(arg0); }' -p $SERVER_PID > ./profile/raw_allocations.out

./FlameGraph/stackcollapse-bpftrace.pl ./profile/raw_allocations.out > ./profile/collapsed_allocations.txt
./FlameGraph/flamegraph.pl --countname=bytes ./profile/collapsed_allocations.txt > ./profile/memory_profile.svg

```

### CPU Usage FlameGraph

Identify hot-paths in the event loop or frame parser.

```bash
SERVER_PID=$(pgrep -f ./build/v4l2_mjpeg_server)
mkdir -p ./profile
rm -f ./profile/*.*

echo "Generating CPU flame graph for PID $SERVER_PID. Put the server under load. Press Ctrl+C to output collected traces."

sudo bpftrace -e 'profile:hz:99 { @[ustack] = count(); }' -p $SERVER_PID > ./profile/cpu_raw.out

sudo ./FlameGraph/stackcollapse-bpftrace.pl ./profile/cpu_raw.out > ./profile/cpu_collapsed.txt
sudo ./FlameGraph/flamegraph.pl ./profile/cpu_collapsed.txt > ./profile/cpu_flamegraph.svg

```

---

## 4. BPFTrace Scripts (Micro-Profiling)

Once you understand the macro-behavior from the FlameGraphs, use these real-time `bpftrace` scripts to monitor specific systems interfaces and latency boundaries.

_Tip: You can list all available system tracepoints by running `sudo bpftrace -l`, or get verbose arguments for a specific tracepoint via `sudo bpftrace -lv tracepoint:syscalls:sys_enter_write`._

### Trace Network Write Latency

Because clients are often connected over Wi-Fi, wireless retransmissions or slow clients can cause `uWebSockets` kernel buffers to fill up. Monitor `sys_enter_sendto` latency to track exactly how long the kernel takes to accept your payload:

```bash
SERVER_PID=$(pgrep -f ./build/v4l2_mjpeg_server)

sudo bpftrace -e '
tracepoint:syscalls:sys_enter_sendto /pid == '$SERVER_PID'/ {
    @start[tid] = nsecs;
    @bytes[tid] = args->len;
}
tracepoint:syscalls:sys_exit_sendto /pid == '$SERVER_PID' && @start[tid]/ {
    $duration = (nsecs - @start[tid]) / 1000;
    @write_latency_us = hist($duration);
    @write_sizes = hist(@bytes[tid]);
    delete(@start[tid]);
    delete(@bytes[tid]);
}'

```

**What to look for**: Bimodal latency distributions. If write operations occasionally take milliseconds instead of microseconds, your Wi-Fi interface queue is saturating.

### Trace Socket Buffer Exhaustion (wmem Saturation)

If the Wi-Fi network drops packets, the TCP window shrinks. `uWebSockets` will invoke `sendto()`, and the kernel will return `EAGAIN` (`-11`). This forces the event loop to waste power tracking epoll events instead of moving Video Frames.

```bash
SERVER_PID=$(pgrep -f ./build/v4l2_mjpeg_server)

sudo bpftrace -e '
tracepoint:syscalls:sys_exit_sendto /pid == '$SERVER_PID' && args->ret == -11/ {
    @[ustack, "EAGAIN"] = count();
}'

```

### Trace Asynchronous Event Loop Latency (epoll)

`uWebSockets` relies entirely on `epoll_wait` to handle non-blocking networking. If the event loop stalls, network packets stall.

```bash
SERVER_PID=$(pgrep -f ./build/v4l2_mjpeg_server)

sudo bpftrace -e '
tracepoint:syscalls:sys_enter_epoll_wait /pid == '$SERVER_PID'/ {
    @loop_start[tid] = nsecs;
}
tracepoint:syscalls:sys_exit_epoll_wait /pid == '$SERVER_PID' && @loop_start[tid]/ {
    $loop_time = (nsecs - @loop_start[tid]) / 1000;
    @epoll_wait_duration_us = hist($loop_time);
    delete(@loop_start[tid]);
}'

```

### Trace V4L2 Hardware Command Latency (ioctl)

The `v4l2_mjpeg_server` fetches raw hardware data from the Linux `/dev/video*` subsystem using `ioctl` calls. Spikes here indicate the hardware or driver is stalling.

```bash
SERVER_PID=$(pgrep -f ./build/v4l2_mjpeg_server)

sudo bpftrace -e '
tracepoint:syscalls:sys_enter_ioctl /pid == '$SERVER_PID'/ {
    @start[tid] = nsecs;
    @cmd_counts[args->cmd] = count();
}
tracepoint:syscalls:sys_exit_ioctl /pid == '$SERVER_PID' && @start[tid]/ {
    $duration = (nsecs - @start[tid]) / 1000;
    @ioctl_latency_us = hist($duration);
    delete(@start[tid]);
}'

```

**What to Look For**: Values in the thousands (milliseconds). Since Video Frames usually capture at 30 FPS, an `ioctl` call waiting for a camera frame might normally take 33,000 microseconds. Spikes significantly higher indicate a hardware drop.

### Trace Mutex Lock Contention

Ensure your disruptor threads aren't suffering from lock contention. _(Note: The libc path may vary depending on your OS)._

```bash
sudo bpftrace -e '
uprobe:/lib/aarch64-linux-gnu/libc.so.6:pthread_mutex_lock /comm == "v4l2_mjpeg_serv"/ {
    @lock_attempts = count();
    @start[tid] = nsecs;
}

uprobe:/lib/aarch64-linux-gnu/libc.so.6:pthread_mutex_unlock /comm == "v4l2_mjpeg_serv" && @start[tid]/ {
    $hold_time = (nsecs - @start[tid]);
    @lock_hold_duration_ns = hist($hold_time);
    delete(@start[tid]);
}'

```

### Live Network Bandwidth Monitor

Track total outbound megabits per second across the streaming server.

```bash
sudo bpftrace -e '
tracepoint:syscalls:sys_enter_sendto /comm == "v4l2_mjpeg_serv"/ {
    @bytes_per_sec = sum(args->len);
    @total_bytes = sum(args->len);
}

interval:s:1 {
    $mbps = (@bytes_per_sec * 8) / 1000000;
    time("%H:%M:%S -> ");
    printf("Bandwidth Consumption: %d Mbps\n", $mbps);
    clear(@bytes_per_sec);
}

END {
    $total_mb = @total_bytes / 1024 / 1024;
    printf("\n--- Session Complete ---\nTotal Data Transferred: %d MB\n", $total_mb);
    clear(@bytes_per_sec);
    clear(@total_bytes);
}'

```

### Live Application Backpressure Monitor

This script hooks into standard output (`sys_enter_write` to `stdout`) to scrape your application's custom backpressure telemetry logs in real-time, matching them alongside physical network writes.

```bash
sudo bpftrace -e '
tracepoint:syscalls:sys_enter_sendto /comm == "v4l2_mjpeg_serv"/ {
    @network_writes_per_sec = count();
}

tracepoint:syscalls:sys_enter_write /comm == "v4l2_mjpeg_serv" && (args->fd == 1 || args->fd == 2)/ {
    $str = str(args->buf);

    // Check for specific substrings in your application logs
    if (strncmp($str, "[Network Telemetry]", 19) == 0) {
        @tcp_stalls_total = count();
    }
    if (strncmp($str, "[Network Core] Evicting", 23) == 0) {
        @client_evictions_total = count();
    }
}

interval:s:1 {
    time("%H:%M:%S -> \n");
    print(@network_writes_per_sec);
    print(@tcp_stalls_total);
    print(@client_evictions_total);
    clear(@network_writes_per_sec);
}'

```

**What This Teaches You On-Call:**

- **The Cost of Batching:** Inside your timer loop, you call `res->cork(...)`. This tells `uWebSockets` to batch text headers and binary arrays together. This is why `strace` or BPF shows exactly 1 `sendto` call per browser tab per frame, rather than multiple fragmented network writes.
- **Identifying Bad Clients:** If a remote user opens your stream on a weak mobile data connection, this script will immediately show an explosion in `tcp_stalls_total`. You can observe your server managing this state gracefully—dropping frames dynamically or triggering an eviction—while healthy local browser tabs continue to receive a smooth, 30FPS stream.
