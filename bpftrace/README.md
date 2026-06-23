## Prerequisites

The linux kernel needs to be compiled with BTF support enabled, and uprobes-based dynamic events
tracer. See [Recompiling Kernel](../../docs/RecompileKernel.md) for details on how to do this.

## Running bpftrace scripts

Run bpftrace as root, passing the path to the script as an argument

```bash
sudo bpftrace ./bpf-stats.bt

```

## Saving bpftrace output

If your bpftrace script generates a lot of data, saving it to disk for post processing can be done
by adding the flags: -B none (no buffering); and -o (output file).

```bash
sudo bpftrace -B none -o malloc_report.txt ./bpf-stats.bt

```
