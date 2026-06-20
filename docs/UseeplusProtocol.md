# Useeplus Protocol

## Binary Stream Capture

Running a build will generate all target binaries, including `mjpeg_stream_capture`. This is a macOS-compatible command-line utility that allows you to select an attached camera and stream the incoming hardware data directly to a binary file—`raw_camera_dump.bin`—in the current working directory.

**Build Script**

```bash
# Build the core executables using Zig and Make
make

```

**Capturing Stream Data**
To save the raw camera stream to a binary file for debugging and protocol analysis:

```bash
./build/mjpeg_stream_capture

```

To view the raw camera data, use the `xxd` command to convert the binary file to hex, then pipe it to `grep`.

Because the Useeplus protocol utilizes little-endian byte order for its 16-bit headers, the OSI Layer 2 Start Frame Delimiter (`0xBBAA`) appears on the wire as `aa bb`. Here is a command that searches the binary file for this exact sequence to locate the start of a USB Frame. Each line represents 16 bytes of data, grouped into 2-byte columns.

```bash
xxd raw_camera_dump.bin | grep -A 2 -B 2 "aa bb" | head -n 30

```

Here is an example of the output:

```text
000235e0: c007 d690 9084 e28e 7bd1 60b7 5171 c537  ........{.`.Qq.7
000235f0: a734 fa02 dac1 bb3d b349 9dc3 b8e6 919d  .4.....=.I......
00023600: b50e 0f5a 6d05 5968 7fff d9aa bb0b ab03  ...Zm.Yh........
00023610: 0800 0060 3330 24ff d8ff e000 104a 4649  ...`30$......JFI
00023620: 4600 0102 0100 4800 4800 00ff db00 8400  F.....H.H.......
--

```

## The Protocol Breakdown

By mapping this hex dump to our C++ implementation, we can decode the dual-layer encapsulation of the Useeplus hardware behavior:

- **The Hardware Handshake (`VENDOR_PRODUCT_ID_LIST`):** Before this stream even begins, `libusb` locates the hardware using specific vendor and product IDs (e.g., `0x2ce3:0x3828` or `0x0329:0x2022`).
- **The Start Frame Delimiter (`UP_PKT_DEL`):** The sequence `aa bb` (`0xBBAA`) acts as the Layer 2 hardware signature, marking the absolute beginning of a new USB Frame.
- **The Device ID (`VALID_DEVICE_IDS`):** The Useeplus hardware multiplexes two separate data streams over a single shared bulk endpoint:
- Device `11` (`0x0B`) is the **Video Feed** (transmitting USB Frames with a declared payload length of 939 bytes).
- Device `7` (`0x07`) is the **Gravity Sensor Telemetry Feed** (transmitting 427-byte payloads).
- To prevent injecting raw telemetry data directly into the Huffman-encoded JPEG stream, the decoder explicitly discards USB Frames matching Device 7 or frames where telemetry flags are active.

- **The JPEG SOI Marker (`JPEG_SOI`):** The sequence `ff d8` is the universal JPEG Start of Image (SOI) marker. Due to uninitialized hardware garbage padding, our decoder must actively scan the start of the Video Frame buffer for this marker before broadcasting.
- **The JPEG EOI Marker (`JPEG_EOI`):** The two bytes immediately preceding a fresh `aa bb` delimiter are usually `ff d9`. This is the universal JPEG End of Image (EOI) marker, cleanly terminating the active Layer 6 Video Frame.
- **The App0 Segment:** Immediately after the SOI marker, the sequence `ff e0` followed by `4a 46 49 46 00` translates to `JFIF` in ASCII, confirming the payload is a standard JPEG container.

## The Dual-Layer Protocol Overhead

The total protocol overhead is exactly 12 bytes long. It is split into two distinct structs mapping to the OSI Link and Presentation layers.

### 1. The USB Frame Header (Layer 2)

The hardware transmission begins with a 5-byte `up_usb_frm_hdr` (2-byte delimiter, 1-byte device ID, and a 2-byte length specifier):

```c
struct up_usb_frm_hdr {
    __le16 le_delimiter;
    u8     device_id;
    __le16 le_length;
} __attribute__((packed));

```

### 2. The Video Frame Fragment Header (Layer 6)

Immediately following the 5-byte transport header, the camera inserts exactly 7 bytes of proprietary presentation-layer metadata before the actual JPEG pixels begin.

```c
struct up_video_frm_frag_hdr {
    u8     frame_id;
    u8     device_number;
    u8     flags;
    __le32 le_gravity_sensor;
} __attribute__((packed));

```

- **The Frame ID:** This is a sequential 8-bit identifier. A complete MJPEG Video Frame is split across multiple fragments. The exact moment the `frame_id` increments, the state machine knows the previous image has finished transmitting and triggers a frame flush to the network loop.
- **The Button Press Flag:** The physical hardware button flips the second bit of the `flags` byte (0x02) to `1` for the duration of a press event.

Let's look at the 12 bytes of Protocol Overhead preceding the JPEG SOI (`ff d8`) from our hex dump:
`aa bb 0b ab 03` **`02 00 00 60 33 30 24`** `ff d8...`

Using the `packed` attribute forces the compiler to lay these out without padding. Because the combined size is exactly 12 bytes, we can define a constant `VIDEO_DATA_OFFSET` = 12. The raw JPEG payload **always begins exactly 12 bytes from the start of the USB Frame**.

## Assembling a Complete Video Frame

A single Layer 2 USB Frame does not contain a full image.

- **The Payload Math:** Each video USB Frame declares a total length of **939 bytes** (`ab 03` in Little-Endian). Subtracting the 7 bytes consumed by the `up_video_frm_frag_hdr` leaves exactly **932 bytes** of pure Video Frame Fragment (JPEG data) per USB Frame.
- **The Frame Size:** At 640x480 resolution, a single compressed MJPEG Video Frame ranges from **15KB to 40KB**.
- **The Assembly:** To transmit a 20KB image, the camera sends roughly 22 consecutive USB Frames. The `frame_id` remains constant across all chunks belonging to the same image. The `MjpegStream` decoder continuously jumps past the 12-byte `VIDEO_DATA_OFFSET` and appends the 932-byte fragments to the LMAX Disruptor memory pool. When the `frame_id` increments, the decoder filters out any padded tails before flushing the completed image to the broadcast queue.

## The 4KB Hardware Alignment Flaw (Ghost Headers)

The camera's physical endpoint forces all transmissions to align with standard **4096-byte (4KB) USB bulk transfer memory pages**.

To fit four 944-byte physical USB Frames (12 bytes of protocol overhead + 932 bytes of fragment payload) into a 4096-byte memory page, the camera's firmware must inject 320 bytes of padding (4096 - (4 \* 944) = 320). The firmware distributes this dynamically, leaving unaligned gaps of 0, 80, or 160 bytes between individual chunks.

Because the firmware fails to zero-initialize this padding, the camera leaks stale memory from its internal hardware buffer, creating **"Ghost Headers"** (stale `0xBBAA` Start Frame Delimiters) inside the padding.

Our C++ `MjpegStream` bypasses this flaw by operating on fixed, linear 4KB read blocks. By calculating `chunkTotalSize = sizeof(up_usb_frm_hdr) + le_length`, the parser processes the exact boundaries of a valid USB Frame. It then uses a bounded look-ahead scan to detect ghost headers in the padding zone before they can corrupt the MJPEG stream parser, ensuring stable, zero-leak video synchronization.

## Useeplus Dual-Layer Protocol Map

| Byte Address | OSI Layer      | Field Name              | Hex Value Example | Description / C Struct Field mapping                         |
| ------------ | -------------- | ----------------------- | ----------------- | ------------------------------------------------------------ |
| 00 - 01      | **L2 (USB)**   | Start Frame Delimiter   | aa bb             | `0xBBAA` (Little-Endian) `le_delimiter`                      |
| 02           | **L2 (USB)**   | Device ID               | 0b                | `0x0B` = Video Feed, `0x07` = Gravity Telemetry              |
| 03 - 04      | **L2 (USB)**   | Payload Length          | ab 03             | Total remaining bytes in USB Frame (`0x03AB` = 939B)         |
| ---          | ---            | ---                     | ---               | ---                                                          |
| 05           | **L6 (Video)** | Video Frame ID          | 08                | Increments when a complete MJPEG image finishes transmitting |
| 06           | **L6 (Video)** | Device Number           | 00                | Secondary internal lens index routing                        |
| 07           | **L6 (Video)** | Hardware Flags          | 00                | Bit 0: hasGravitySensor, Bit 1: isButtonPressed              |
| 08 - 11      | **L6 (Video)** | IMU Telemetry Matrix    | 60 33 30 24       | 32-bit `le_gravity_sensor` accelerometer payload             |
| ---          | ---            | ---                     | ---               | ---                                                          |
| 12 - 13      | **L6 (Video)** | JPEG SOI Marker         | ff d8             | `VIDEO_DATA_OFFSET` Boundary. Universal JPEG Start of Image  |
| 14 - 15      | **L6 (Video)** | JPEG APP0 Header Marker | ff e0             | JFIF                                                         |
| 16 - 17      | **L6 (Video)** | APP0 Segment Length     | 00 10             | 16 bytes                                                     |
| 18 - 22      | **L6 (Video)** | APP0: Identifier        | 4a 46 49 46 00    | ASCII "JFIF\0"                                               |
| 23 - 24      | **L6 (Video)** | APP0: Version           | 01 02             | JFIF 1.02                                                    |
| 25           | **L6 (Video)** | APP0: Density Units     | 01                | 1 DPI                                                        |
| 26 - 27      | **L6 (Video)** | APP0: X Density         | 00 48             | Big-Endian `0x0048` = 72 DPI                                 |
| 28 - 29      | **L6 (Video)** | APP0: Y Density         | 00 48             | Big-Endian `0x0048` = 72 DPI                                 |
| 30           | **L6 (Video)** | APP0: Thumbnail Width   | 00                | 0 pixels                                                     |
| 31           | **L6 (Video)** | APP0: Thumbnail Height  | 00                | 0 pixels                                                     |
| 32 - 941     | **L6 (Video)** | Huffman Stream Data     | Variable          | Raw MJPEG Video Frame Fragment (910 Bytes per USB Frame)     |
| 942 - 943    | **L6 (Video)** | JPEG EOI Marker         | ff d9             | Universal JPEG End of Image Boundary                         |
