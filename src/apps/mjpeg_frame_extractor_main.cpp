#include <bit>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <fstream>
#include <string>

namespace Units {
    inline constexpr size_t ONE_KILOBYTE = 1024;
    inline constexpr size_t TWO_HUNDRED_FIFTY_SIX_KILOBYTES = 256 * ONE_KILOBYTE;
}

namespace UsbProtocol {
    inline constexpr uint8_t GRAVITY_SENSOR_CAMERA_ID = 0x07;
    inline constexpr uint8_t VIDEO_CAMERA_ID = 0x0B;
    inline constexpr uint8_t USB_FRAME_HEADER_A = 0xAA;
    inline constexpr uint8_t USB_FRAME_HEADER_B = 0xBB;
    inline constexpr uint8_t BOUNDARY_MARKER = 0xFF;
    inline constexpr uint8_t START_MARKER = 0xD8;
    inline constexpr uint8_t END_MARKER = 0xD9;
}

constexpr size_t MAX_FRAME_SIZE = Units::TWO_HUNDRED_FIFTY_SIX_KILOBYTES;

constexpr uint8_t wireToHost(uint8_t val) noexcept { return val; }
template <std::integral T>
constexpr T wireToHost(T val) noexcept {
    if constexpr (std::endian::native == std::endian::big) { return std::byteswap(val); }
    return val;
}

#pragma pack(push, 1)
struct [[gnu::packed]] UsbPacketHeader {
    uint16_t leHeader;
    uint8_t leCameraId;
    uint16_t leLength;

    constexpr uint16_t getHeader() const noexcept { return wireToHost(leHeader); }
    constexpr uint8_t getCameraId() const noexcept { return wireToHost(leCameraId); }
    constexpr uint16_t getLength() const noexcept { return wireToHost(leLength); }
};

struct [[gnu::packed]] UsbPayloadHeader {
    uint8_t leFrameId;
    uint8_t leCameraNumber;
    uint8_t leFlags;
    uint32_t leGravitySensor;

    constexpr uint8_t getFrameId() const noexcept { return wireToHost(leFrameId); }
    constexpr uint8_t getCameraNumber() const noexcept { return wireToHost(leCameraNumber); }
    constexpr uint8_t getFlags() const noexcept { return wireToHost(leFlags); }
    constexpr bool hasGravitySensor() const noexcept { return (getFlags() & 0x01) != 0; }
    constexpr uint8_t getOtherFlags() const noexcept { return (getFlags() >> 2) & 0x3F; }
};
#pragma pack(pop)

inline constexpr size_t USB_PACKET_HEADER_SIZE = sizeof(UsbPacketHeader);
inline constexpr size_t USB_PAYLOAD_HEADER_SIZE = sizeof(UsbPayloadHeader);
inline constexpr size_t TOTAL_USB_HEADER_SIZE = USB_PACKET_HEADER_SIZE + USB_PAYLOAD_HEADER_SIZE;

enum class ParseState: uint8_t {
    FIND_HEADER_A,
    FIND_HEADER_B,
    READ_PACKET_HEADER,
    READ_PAYLOAD_HEADER,
    STREAM_VIDEO,
    SKIP_TELEMETRY
};

int main(int argc, const char* argv[]) {
    try {
        if (argc != 3) return EXIT_FAILURE;

        int64_t targetFrameNumber = std::stoll(argv[2]);
        if (targetFrameNumber < 1) return EXIT_FAILURE;

        std::string inputPath(argv[1]);
        std::ifstream file(inputPath, std::ios::binary);
        if (!file.is_open()) return EXIT_FAILURE;

        constexpr size_t transferSize = Units::ONE_KILOBYTE;
        uint8_t transferBuffer[transferSize];

        static uint8_t currentFrame[MAX_FRAME_SIZE];
        size_t currentFrameLen = 0;
        int validFrameCount = 0;

        ParseState state = ParseState::FIND_HEADER_A;
        uint8_t headerBuffer[TOTAL_USB_HEADER_SIZE];
        size_t headerBytesCollected = 0;
        
        uint8_t activeCameraId = 0;
        size_t payloadBytesRemaining = 0;
        int lastFrameId = -1;

        while (file.read(reinterpret_cast<char*>(transferBuffer), transferSize) || file.gcount() > 0) {
            std::streamsize bytesInTransfer = file.gcount();
            if (bytesInTransfer < 0) return EXIT_FAILURE;

            for (std::streamsize idx = 0; idx < bytesInTransfer; ++idx) {
                uint8_t b = transferBuffer[idx]; // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)

                switch (state) {
                    case ParseState::FIND_HEADER_A:
                        if (b == UsbProtocol::USB_FRAME_HEADER_A) {
                            state = ParseState::FIND_HEADER_B;
                        }
                        break;

                    case ParseState::FIND_HEADER_B:
                        if (b == UsbProtocol::USB_FRAME_HEADER_B) {
                            headerBuffer[0] = UsbProtocol::USB_FRAME_HEADER_A;
                            headerBuffer[1] = UsbProtocol::USB_FRAME_HEADER_B;
                            headerBytesCollected = 2;
                            state = ParseState::READ_PACKET_HEADER;
                        } else if (b != UsbProtocol::USB_FRAME_HEADER_A) {
                            state = ParseState::FIND_HEADER_A;
                        }
                        break;

                    case ParseState::READ_PACKET_HEADER:
                        headerBuffer[headerBytesCollected++] = b; // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
                        if (headerBytesCollected == USB_PACKET_HEADER_SIZE) {
                            const auto* pkt = reinterpret_cast<const UsbPacketHeader*>(headerBuffer);
                            activeCameraId = pkt->getCameraId();
                            payloadBytesRemaining = pkt->getLength();

                            if (activeCameraId == UsbProtocol::VIDEO_CAMERA_ID || 
                                activeCameraId == UsbProtocol::GRAVITY_SENSOR_CAMERA_ID) {
                                state = ParseState::READ_PAYLOAD_HEADER;
                            } else {
                                state = ParseState::FIND_HEADER_A;
                            }
                        }
                        break;

                    case ParseState::READ_PAYLOAD_HEADER:
                        headerBuffer[headerBytesCollected++] = b; // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
                        payloadBytesRemaining--;

                        if (headerBytesCollected == TOTAL_USB_HEADER_SIZE) {
                            const auto* payload = reinterpret_cast<const UsbPayloadHeader*>(headerBuffer + USB_PACKET_HEADER_SIZE);

                            if (lastFrameId != -1 && payload->getFrameId() != static_cast<uint8_t>(lastFrameId)) {
                                if (currentFrameLen > 0) {
                                    validFrameCount++;
                                    
                                    if (validFrameCount == targetFrameNumber) {
                                        std::string filename = std::format("frame_{:04d}.jpg", validFrameCount);
                                        std::ofstream image(filename, std::ios::binary);
                                        image.write(reinterpret_cast<const char*>(currentFrame), currentFrameLen);
                                        return EXIT_SUCCESS;
                                    }
                                    currentFrameLen = 0;
                                }
                            }

                            lastFrameId = payload->getFrameId();

                            if (activeCameraId == UsbProtocol::VIDEO_CAMERA_ID && 
                                !payload->hasGravitySensor() && 
                                payload->getOtherFlags() == 0 && 
                                payload->getCameraNumber() < 2) {
                                state = ParseState::STREAM_VIDEO;
                            } else {
                                state = ParseState::SKIP_TELEMETRY;
                            }
                        }
                        break;

                    case ParseState::STREAM_VIDEO:
                        if (currentFrameLen < MAX_FRAME_SIZE) {
                            currentFrame[currentFrameLen++] = b; // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
                        }
                        payloadBytesRemaining--;
                        
                        if (payloadBytesRemaining == 0) {
                            state = ParseState::FIND_HEADER_A;
                        }
                        break;

                    case ParseState::SKIP_TELEMETRY:
                        payloadBytesRemaining--;
                        if (payloadBytesRemaining == 0) {
                            state = ParseState::FIND_HEADER_A;
                        }
                        break;
                }
            }
        }
    }
    catch (...) {
        return EXIT_FAILURE;
    }
    return EXIT_FAILURE;
}
