# Profiling using BPF and FlameGraphs

Finding performance bottlenecks in C++ code is not as complicated as you might think, using modern tools such as BPF 
and FlameGraphs. The most complicated part is configuring the Linux kernel to enable BTF Type Info and uprobes. This 
may involve recompiling the kernel - for example on a Raspberry PI with the default OS install.

Here's a quick primer on [recompiling the Raspberry Pi Kernel](RecompileKernel.md) to add the needed options for BPF.

## Linux Kernel Configs

If you want to use eBPF, for example to generate FlameGraphs, you'll need to enable BTF (BPF Type Format) Support. 
Here's the options that need to be configured in Raspberry Pi kernel configs when re-compiling, if you are using 
`make menuconfig` to generate the .config file. 

- Enable "Kernel hacking -> Compile-time checks and compiler options -> Compile the Kernel with debug info"
  - Select "Rely on the toolchain's implicit default DWARF version"
- Enable "Kernel hacking -> Compile-time checks and compiler options -> Generate BTF typeinfo"
- Enable "Kernel hacking -> Tracers -> Enable uprobes-based dynamic events"

## Compiler Flags

If you want to run BPF / linux perf on custom applications you are building, ensure your CMake target configurations inject the following flags:
- `-g`: Embeds your source code maps into the build.
- `-fno-omit-frame-pointer`: Instructs the compiler to keep the frame pointer register on the stack for every function call.

In our unified `CMakeLists.txt`, these options are mapped to targets built on Linux architectures:

```cmake
if(NOT APPLE)
    target_compile_options(pi-borescope-streamer PRIVATE 
        -g                        # Generate debug symbols
        -fno-omit-frame-pointer   # Keep frame pointers for deep stack walks
    )
endif()
```

To run a production-ready profile with these optimizations enabled, always build the project using the optimized release preset:

```bash
cmake . --preset release
cmake --build --preset release
```

## Clone Brendan Gregg's FlameGraph Repo

```bash
git clone https://github.com/brendangregg/FlameGraph.git

```

## Install bpftrace

```bash
sudo apt update
sudo apt install bpftrace
```

## Install linux-perf

```bash
sudo apt update
sudo apt install linux-perf
```

## Generate Memory Allocation FlameGraph

```bash
# Locate the running instance of the new CMake-built server executable
SERVER_PID=$(pgrep -f ./out/build/release/pi-borescope-streamer)

echo "Generating memory allocation flame graph for PID $SERVER_PID. Put the server under load. Ctrl+C to output collected traces"

rm ./profile/*.*

sudo bpftrace -e 'uprobe:libc:malloc { @[ustack] = sum(arg0); }' -p $SERVER_PID > ./profile/raw_allocations.out

../FlameGraph/stackcollapse-bpftrace.pl ./profile/raw_allocations.out > ./profile/collapsed_allocations.txt

../FlameGraph/flamegraph.pl --countname=bytes ./profile/collapsed_allocations.txt > ./profile/memory_profile.svg
```

## Generate CPU FlameGraphs

```bash
# Locate the running instance of the new CMake-built server executable
SERVER_PID=$(pgrep -f ./out/build/release/pi-borescope-streamer)

echo "Generating CPU flame graph for PID $SERVER_PID. Put the server under load. Ctrl+C to output collected traces"

rm ./profile/*.*

sudo bpftrace -e 'profile:hz:99 { @[ustack] = count(); }' -p $SERVER_PID > ./profile/cpu_raw.out

sudo ../FlameGraph/stackcollapse-bpftrace.pl ./profile/cpu_raw.out > ./profile/cpu_collapsed.txt

sudo ../FlameGraph/flamegraph.pl ./profile/cpu_collapsed.txt > ./profile/cpu_flamegraph.svg
```

## bpftrace 

```bash
# bpftrace has a lot of functionality, and can be a little daunting at first. I find it helpful to tool at some 
# examples, then come back to the docs to solidify my understanding. 
$ bpftrace --help
USAGE:
    bpftrace [options] filename
    bpftrace [options] - <stdin input>
    bpftrace [options] -e 'program'

OPTIONS:
    -B MODE        output buffering mode ('line', 'full', 'none')
    -f FORMAT      output format ('text', 'json')
    -o file        redirect bpftrace output to file
    -e 'program'   execute this program
    -h, --help     show this help message
    -I DIR         add the directory to the include search path
    --include FILE add an #include file before preprocessing
    -l [search|filename]
                   list kernel probes or probes in a program
    -p PID         enable USDT probes on PID
    -c 'CMD'       run CMD and enable USDT probes on resulting process
    --usdt-file-activation
                   activate usdt semaphores based on file path
    --unsafe       allow unsafe/destructive functionality
    -q             keep messages quiet
    --info         Print information about kernel BPF support
    -k             emit a warning when probe read helpers return an error
    -V, --version  bpftrace version
    --no-warnings  disable all warning messages

TROUBLESHOOTING OPTIONS:
    -v                      verbose messages
    --dry-run               terminate execution right after attaching all the probes
    -d STAGE                debug info for various stages of bpftrace execution
                            ('all', 'ast', 'codegen', 'codegen-opt', 'dis', 'libbpf', 'verifier')
    --emit-elf FILE         (dry run) generate ELF file with bpf programs and write to FILE
    --emit-llvm FILE        write LLVM IR to FILE.original.ll and FILE.optimized.ll

ENVIRONMENT:
    BPFTRACE_BTF                      [default: none] BTF file
    BPFTRACE_CACHE_USER_SYMBOLS       [default: auto] enable user symbol cache
    BPFTRACE_COLOR                    [default: auto] enable log output colorization
    BPFTRACE_CPP_DEMANGLE             [default: 1] enable C++ symbol demangling
    BPFTRACE_DEBUG_OUTPUT             [default: 0] enable bpftrace's internal debugging outputs
    BPFTRACE_KERNEL_BUILD             [default: /lib/modules/$(uname -r)] kernel build directory
    BPFTRACE_KERNEL_SOURCE            [default: /lib/modules/$(uname -r)] kernel headers directory
    BPFTRACE_LAZY_SYMBOLICATION       [default: 0] symbolicate lazily/on-demand
    BPFTRACE_LOG_SIZE                 [default: 1000000] log size in bytes
    BPFTRACE_MAX_BPF_PROGS            [default: 1024] max number of generated BPF programs
    BPFTRACE_MAX_CAT_BYTES            [default: 10k] maximum bytes read by cat builtin
    BPFTRACE_MAX_MAP_KEYS             [default: 4096] max keys in a map
    BPFTRACE_MAX_PROBES               [default: 1024] max number of probes
    BPFTRACE_MAX_STRLEN               [default: 1024] bytes on BPF stack per str()
    BPFTRACE_MAX_TYPE_RES_ITERATIONS  [default: 0] number of levels of nested field accesses for tracepoint args
    BPFTRACE_PERF_RB_PAGES            [default: 64] pages per CPU to allocate for ring buffer
    BPFTRACE_STACK_MODE               [default: bpftrace] Output format for ustack and kstack builtins
    BPFTRACE_STR_TRUNC_TRAILER        [default: '..'] string truncation trailer
    BPFTRACE_VMLINUX                  [default: none] vmlinux path used for kernel symbol resolution

EXAMPLES:
bpftrace -l '*sleep*'
    list probes containing "sleep"
bpftrace -e 'kprobe:do_nanosleep { printf("PID %d sleeping...\n", pid); }'
    trace processes calling sleep
bpftrace -e 'tracepoint:raw_syscalls:sys_enter { @[comm] = count(); }'
    count syscalls by process name
```

## Trace Network Write Latency and Blockages

```bash
# Capture syscall events across process threads
sudo strace -fp 2082 -c
```

Because clients are connected over Wi-Fi, wireless retransmissions or slow clients can cause uWebSockets kernel 
buffers to fill up. When buffers fill up, the mjpeg streaming server blocks or wastes cycles polling. Monitor 
sys_enter_write and sys_exit_write latency to track exactly how much data is being passed to the network socket, and 
how long the kernel takes to accept that payload:

```bash
# Locate the running instance of the new CMake-built server executable
SERVER_PID=$(pgrep -f ./out/build/release/pi-borescope-streamer)

# Capture write latency and write sizes, and print out a histogram of both metrics
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

**What to look for**: Bimodal latency distributions. If write operations occasionally take milliseconds instead of 
microseconds, your Wi-Fi interface queue is saturating.

## The ioctl Latency and Command Profiler

Run this script while your video stream is running, let it capture data for a few seconds, and press CTRL+C:

```bash
# Capture ioctl latencies and generate a histogram
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

**What to Look For in the Results**: 
- @ioctl_latency_us: Look for values reaching into the thousands (milliseconds). Since video frames usually capture at 
30 or 60 FPS, an ioctl call waiting for a camera frame might normally take roughly 16,000 to 33,000 microseconds. 
Spikes significantly higher than that indicate the hardware or the driver is stalling.
- @cmd_counts: This will print a list of hex or decimal command codes. The code with the highest count is the loop 
fetching your video data (likely a V4L2 buffer dequeue command).Run this version and paste the charts when you are 
ready.

If we find high latency spikes here, would you like me to show you how to extract the specific V4L2 driver command 
names from those numeric command codes?

## Locks 

```bash
# Get the shared libraries used by pi-borescope-streamer
 ldd /home/jterry/github/pi-borescope-streamer/out/build/release/pi-borescope-streamer | grep libc.so
```

```bash
# Capture lock attempts and amount of time locks are held (nanoseconds)
sudo bpftrace -e '
uprobe:/lib/aarch64-linux-gnu/libc.so.6:pthread_mutex_lock /comm == "pi-borescope-st"/ {
    @lock_attempts = count();
    @start[tid] = nsecs;
}

uprobe:/lib/aarch64-linux-gnu/libc.so.6:pthread_mutex_unlock /comm == "pi-borescope-st" && @start[tid]/ {
    $hold_time = (nsecs - @start[tid]);
    @lock_hold_duration_ns = hist($hold_time);
    delete(@start[tid]);
}'
```

## Network Bandwidth Consumption

```bash
sudo bpftrace -e '
tracepoint:syscalls:sys_enter_sendto /comm == "pi-borescope-st"/ {
    @bytes_per_sec = sum(args->len);
    @total_bytes = sum(args->len);
}

interval:s:1 {
    // Convert bytes/sec to Megabits/sec: (Bytes * 8 bits) / 1,000,000 bits
    $mbps = (@bytes_per_sec * 8) / 1000000;
    
    // Print a readable status line
    time("%H:%M:%S -> ");
    printf("Bandwidth Consumption: %d Mbps\n", $mbps);
    
    // Reset the per-second accumulator for the next interval
    clear(@bytes_per_sec);
}

END {
    // When you press CTRL+C, print the grand total transferred
    $total_mb = @total_bytes / 1024 / 1024;
    printf("\n--- Session Complete ---\nTotal Data Transferred: %d MB\n", $total_mb);
    clear(@bytes_per_sec);
    clear(@total_bytes);
}'
```

## Live Network Backpressure Monitor

This script hooks into sys_enter_sendto. Every time the server transmits data, it checks how often it is forced to 
drop frames or stall due to network congestion:

```bash
sudo bpftrace -e '
tracepoint:syscalls:sys_enter_sendto /comm == "pi-borescope-st"/ {
    @network_writes_per_sec = count();
}

// Track stdout (FD 1) and stderr (FD 2) text streams safely
tracepoint:syscalls:sys_enter_write /comm == "pi-borescope-st" && (args->fd == 1 || args->fd == 2)/ {
    $str = str(args->buf);
    if (strfind($str, "TCP stall") != -1) {
        @tcp_stalls_total = count();
    }
    if (strfind($str, "Evicting lagging viewer") != -1) {
        @client_evictions_total = count();
    }
}

interval:s:1 {
    time("%H:%M:%S -> ");
    printf("Network Writes/sec: %d | Active TCP Stalls: %d | Evictions: %d\n", 
           @network_writes_per_sec, @tcp_stalls_total, @client_evictions_total);
    clear(@network_writes_per_sec);
}'
```

```bashc
# List all tracepoints
sudo bpftrace -l
```

```bash
# list verbose details about a tracepoint
sudo bpftrace -lv tracepoint:syscalls:sys_enter_write
```

```bash
sudo bpftrace -e '
tracepoint:syscalls:sys_enter_sendto /comm == "pi-borescope-st"/ {
    @network_writes_per_sec = count();
}

tracepoint:syscalls:sys_enter_write /comm == "pi-borescope-st" && (args->fd == 1 || args->fd == 2)/ {
    $str = str(args->buf);
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

**What This Teaches You On-Call**
- The Cost of res->cork(): Inside your timer loop, you call res->cork(...). This tells uWebSockets to batch your text 
headers, the stringified length, and the binary frame array together into a single cohesive buffer before invoking the 
system call layer. This is why your strace shows exactly 1 sendto call per browser tab per 15ms, rather than multiple 
separate fragmented network writes.
- Identifying Bad Clients: If a remote user opens your stream on a weak mobile data connection, bpftrace will 
immediately show an explosion in @tcp_stalls_total. You can observe your server managing this state gracefully—dropping 
frames dynamically while the other 3 local browser tabs continue to receive smooth, full-frame data streams.

## Trace Socket Buffer Exhaustion (wmem Saturation)

If the Wi-Fi network drops packets, the TCP window shrinks. uWebSockets (via uSockets) will invoke write() or send(), 
and the kernel will return EAGAIN or EWOULDBLOCK. This forces the event loop to waste power tracking epoll events 
instead of moving frames.

**Count Network Buffer Congestion Events**

```bash
sudo bpftrace -e '
tracepoint:syscalls:sys_exit_write* /pid == '$SERVER_PID' && args.ret == -11/ {
    @[ustack, "EAGAIN"] = count();
}'
```

**What to look for**: High counts here mean Wi-Fi clients cannot ingest the MJPEG stream fast enough. The mjpeg 
streaming server is generating frames faster than the wireless medium can clear them, filling up the socket send 
buffers (wmem).

## Trace Asynchronous Event Loop Latency (epoll)

uWebSockets relies entirely on epoll_wait to handle non-blocking networking. If the event loop stalls (e.g., waiting 
on USB transfers), network packets stall. 

**Measure Event Loop Sleep vs. Wake Times**

```bash
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

**What to look for**: Long epoll_wait times mean the server is idle, which is fine. However, short epoll_wait times 
spinning rapidly with 0 returned events indicate an event loop configuration error or inefficient polling.

## Trace libusb and USB Core Submission Latency

Since the Useeplus supercamera is a custom non-UVC device that we are handling with libusb, libusb relies on 
asynchronous usb_submit_urb and usb_kill_urb routines under the hood. We want to see if the delay lies in the hardware 
layer handling the MJPEG payloads.

**Trace Kernel USB Request Block (URB) Completions**

```bash
sudo bpftrace -e '
kprobe:usb_hcd_giveback_urb {
    $urb = (struct urb *)arg0;
    // Filter by your device driver or check global completion latency
    @urb_status[status] = count();
    @urb_transfer_lengths = hist($urb->actual_length);
}'
```

**What to look for**: Keep an eye on actual_length histograms. If MJPEG frames are severely fragmented across many tiny 
USB transfers, it creates continuous context-switch overhead across the Raspberry Pi 5 PCIe/RP1 chip topology.

## eBPF bcc tools

```bash
# Find out what version of llvm-strip is installed
ls /usr/bin/llvm-strip*

# create symbolic link llvm-strip-19->llvm-strip before running make in bcc/libbpf-tools 
sudo ln -s /usr/bin/llvm-strip-19 /usr/bin/llvm-strip

# create symbolic link for Raspberry Pi kernel headers for some tools to build in bcc/libbpf-tools
sudo ln -sf /usr/include/aarch64-linux-gnu/asm /usr/include/asm
```

```bash
git clone --recurse-submodules https://github.com/iovisor/bcc.git
cd bcc/libbpf-tools

# Run make on each tool you want to use
make execsnoop

# Or run make with no arguments to build all libbpf-tools
make

```



