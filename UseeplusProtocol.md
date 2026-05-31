# Useeplus Protocol

## Binary Stream Capture

Running a build will generate all binaries, including `binary_stream_capture`. This is a command line utility that
allows you to select an attached camera, and stream the incoming data to a binary file - `raw_camera_dump.bin` in the
current working directory.

**Build Script**

```bash
cmake . --preset release
cmake --build --preset release

```

**Capturing Stream Data**
To save the raw camera stream to a binary file for debugging and protocol analysis:

```bash
./out/build/release/binary_stream_capture

```

To view the raw camera data, use the `xxd` command to convert the binary file to hex, then pipe it to `grep`.

Because the Useeplus protocol uses little-endian byte order, the C++ constant `0xBBAA` appears on the wire as `aa bb`.
Here is a command that searches the binary file for this exact sequence, which serves as the packet delimiter. Each
line represents 16 bytes of data, grouped into 2-byte columns.

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

By mapping this hex dump to our C++ implementation, we can decode the Useeplus hardware behavior:

* **The Hardware Handshake (`VENDOR_PRODUCT_ID_LIST`):** Before this stream even begins, `libusb` locates the hardware
using specific vendor and product IDs (e.g., `0x2ce3:0x3828` or `0x0329:0x2022`).
* **The Packet Delimiter (`USB_FRAME_HEADER`):** The sequence `aa bb` marks the start of a new USB chunk. Due to Little-Endian architecture, this matches our C++ core definition of `0xBBAA`.
* **The Camera ID (`VALID_CAMERA_IDS`):** The Useeplus hardware multiplexes two separate streams over a single shared bulk endpoint:
  * Camera `11` (`0x0B`) is the **Video Feed** (transmitting packets with a declared payload length of 939 bytes).
  * Camera `7` (`0x07`) is the **Gravity Sensor Telemetry Feed** (transmitting 427-byte payloads).
  * To prevent injecting raw telemetry data directly into the Huffman-encoded JPEG stream, the decoder explicitly discards packets matching Camera 7 or packets where telemetry flags are active.
* **The JPEG SOI Marker (`JPEG_SOI_MARKERS`):** The sequence `ff d8` is the universal JPEG Start of Image (SOI) marker. Due to uninitialized hardware garbage padding, our decoder must actively scan the start of the frame assembly buffer for this marker before broadcasting.
* **The JPEG EOI Marker:** The two bytes immediately preceding a fresh `aa bb` delimiter are `ff d9`. This is the universal JPEG End of Image (EOI) marker, cleanly terminating the active video frame.
* **The App0 Segment:** Immediately after the SOI marker, the sequence `ff e0` followed by `4a 46 49 46 00` translates to `JFIF` in ASCII, confirming the payload is a standard JPEG container.

## The Chunk Metadata (The 7-Byte Payload Header)

The total packet layout begins with a 5-byte `UsbPacketHeader` (2-byte magic identifier, 1-byte camera ID, and a 2-byte payload length specifier):

```cpp
struct [[gnu::packed]] UsbPacketHeader {
    uint16_t header;
    uint8_t cameraId;
    uint16_t length;
};
```

Immediately following the 5-byte header, the camera inserts exactly 7 bytes of proprietary `ChunkMetadata` before the actual JPEG pixels begin. 

Let's look at the 12 bytes preceding the JPEG SOI (`ff d8`) from our hex dump:
`aa bb 0b ab 03` **`02 00 00 60 33 30 24`** `ff d8...`

This 7-byte block (**`02 00 00 60 33 30 24`**) maps directly to our packed business logic struct:

```cpp
struct [[gnu::packed]] ChunkMetadata {
    uint8_t frameId;
    uint8_t cameraNumber;
    unsigned char hasGravitySensor:1;
    unsigned char buttonPress:1;
    unsigned char otherFlags:6;
    uint32_t gravitySensor;
};
```

Using `[[gnu::packed]]` forces the compiler to lay this out in exactly 7 bytes of memory without inserting padding bytes. Combined with the 5-byte packet header, the raw JPEG payload **always begins exactly 12 bytes from the start of the packet**.

* **The Frame ID:** This is a sequential identifier. The exact moment the `frameId` changes, the state machine knows the previous image has finished transmitting and triggers a frame flush.
* **The Button Press Flag:** The physical hardware button flips the `buttonPress` bit to `1` for the duration of a press event.

## Assembling a Complete JPEG Frame

A single USB bulk transfer packet does not contain a full image.

* **The Payload Math:** Each video packet declares a payload length of **939 bytes** (`ab 03` in Little-Endian). Subtracting the 7 bytes consumed by the `ChunkMetadata` leaves exactly **932 bytes** of pure JPEG data per packet.
* **The Frame Size:** At 640x480 resolution, a single compressed MJPEG frame ranges from **15KB to 40KB**.
* **The Assembly:** To transmit a 20KB image, the camera sends roughly 22 consecutive USB packets. The `frameId` remains constant across all chunks belonging to the same image. The `UsbFrameDecoder` continuously appends the 932-byte payloads to `frameBuffer`. When the Frame ID increments, the decoder filters out any padded tails or trailing garbage before flushing the completed image to the broadcast queue.

## The 4KB Hardware Alignment Flaw (Ghost Headers)

The camera's physical endpoint forces all transmissions to align with standard **4096-byte (4KB) USB bulk transfer boundaries**. 

To fit four 944-byte physical packets ($5 \text{ bytes header} + 939 \text{ bytes payload}$) into a 4096-byte memory page, the camera's firmware must inject 320 bytes of padding ($4096 - [4 \times 944] = 320$). The firmware distributes this dynamically, leaving unaligned gaps of 0, 80, or 160 bytes between individual chunks.

Because the firmware fails to zero-initialize this padding, the camera leaks stale memory from its internal hardware buffer, creating **"Ghost Headers"** (stale `AA BB` markers) inside the padding.

Our C++ `UsbFrameDecoder` bypasses this flaw by operating on fixed, linear 4KB read blocks. By calculating `chunkTotalSize = sizeof(UsbPacketHeader) + header.length`, the parser processes the exact boundaries of a valid packet. It then uses a bounded lookahead scan to detect ghost headers in the padding zone before they can corrupt the MJPEG stream parser, ensuring stable, zero-leak video synchronization.

## USB Packet Structure

| Byte Address | Field Name | Hex Value Example | Description / C++ Field mapping |
|---|---|---|---|
| 00 - 01 | Packet Delimiter | aa bb | 0xBBAA (Little-Endian) Magic Frame Header Anchor |
| 02 | Camera Stream ID | 0b | 0x0B = Video Feed, 0x07 = Gravity Telemetry |
| 03 - 04 | Payload Length | ab 03 | Total remaining bytes in packet payload (0x03AB = 939B) |
| 05 | Frame Sequence ID | 08 | Increments when a complete MJPEG frame finish transmitting |
| 06 | Camera Sub-System | 00 | Secondary internal lens index routing |
| 07 | Packed Bitfield Flags | 00 | Bit 0: hasGravitySensor, Bit 1: buttonPress, Bits 2-7: Unused |
| 08 - 11 | Gravity Sensor Matrix | 60 33 30 24 | 32-bit internal IMU accelerometer telemetry payload |
| 12 - 13 | JPEG SOI Marker | ff d8 | Universal JPEG Start of Image Boundary |
| 14 - 31 | JPEG APP0 Segment | ff e0 ... 00 | Injected JFIF-compliant metadata header container |
| 32 - 941 | Huffman Stream Data | Variable | Raw quantization entropy blocks (910 Bytes per packet) |
| 942 - 943 | JPEG EOI Marker | ff d9 | Universal JPEG End of Image Terminal Line Boundary |
