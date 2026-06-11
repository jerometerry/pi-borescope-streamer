# Supercamer Linux Driver

## Prerequisites

```bash
sudo apt update
sudo apt install build-essential linux-headers-$(uname -r)
```

## Building

```bash
make
```

## Verifying Build

```bash
modinfo supercamera.ko
```

## Load Linux Driver 

```bash
sudo insmod supercamera.ko
```