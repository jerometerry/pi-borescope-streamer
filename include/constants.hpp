#pragma once

#include <atomic>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>
#include "usb_packet_header.hpp"
#include "usb_payload_header.hpp"

namespace UsbProtocol {
    /**
     * @brief The primary control interface of the USB device.
     */
    inline constexpr int INTERFACE_A_NUMBER = 0;

    /**
     * @brief The secondary data interface of the USB device.
     */
    inline constexpr int INTERFACE_B_NUMBER = 1;

    /**
     * @brief The alternate setting required to activate the video stream on Interface B.
     */
    inline constexpr int INTERFACE_B_ALTERNATE_SETTING = 1;

    /**
     * @brief The primary USB channel where the heavy video data flows in.
     */
    inline constexpr unsigned char ENDPOINT_1 = 1;

    /**
     * @brief A secondary USB channel used for camera state or hardware button presses.
     */
    inline constexpr unsigned char ENDPOINT_2 = 2;

    inline constexpr uint8_t INITIALIZATION_TOKENS[] = {0xFF, 0x55, 0xFF, 0x55, 0xEE, 0x10};

    inline constexpr uint8_t START_STREAM_TOKENS[] = {0xBB, 0xAA, 5, 0, 0};

    /** 
     * @brief The universal mathematical signature (Start of Image) that begins every valid JPEG file.
     */
    inline constexpr uint8_t JPEG_SOI_MARKERS[] = { 0xFF, 0xD8 };

    /**
     * @brief We will stop searching for the JPEG start signature if we don't find it within the first 32 bytes of a payload.
     */
    inline constexpr size_t JPEG_SOI_MARKERS_MAX_POSITION = 256;

    inline const size_t MAX_SCAN_LIMIT = 300;

    /**
     * @brief The internal hardware lens ID for endoscopes equipped with a physical orientation sensor.
     */
    inline constexpr uint8_t GRAVITY_SENSOR_CAMERA_ID = 0x07;

    /**
     * @brief The internal hardware lens ID for standard, non-gravity video endoscopes.
     */
    inline constexpr uint8_t VIDEO_CAMERA_ID = 0x0B;

    /**
     * @brief A registry of all known internal camera lenses this software knows how to decode.
     */
    inline constexpr uint8_t VALID_CAMERA_IDS[] = {GRAVITY_SENSOR_CAMERA_ID, VIDEO_CAMERA_ID};

    /**
     * @brief The secret 0xBBAA "shipping label" code the hardware uses to tag a valid video chunk on the wire.
     */
    inline constexpr uint16_t USB_FRAME_HEADER = 0xBBAA;
    
    inline constexpr uint8_t USB_FRAME_HEADER_A = 0xAA;
    
    inline constexpr uint8_t USB_FRAME_HEADER_B = 0xBB;

    inline constexpr uint8_t BOUNDARY_MARKER = 0xFF;

    inline constexpr uint8_t START_MARKER = 0xD8;

    inline constexpr uint8_t END_MARKER = 0xD9;

    /**
     * @brief The official hardware whitelist. The software will only connect to cameras matching these Manufacturer and Model IDs.
     */
    inline constexpr std::pair<uint16_t, uint16_t> VENDOR_PRODUCT_ID_LIST[] = {
        {0x2ce3, 0x3828}, 
        {0x0329, 0x2022}
    };
}

namespace Units {
    inline constexpr size_t ONE_KILOBYTE = 1024;
    inline constexpr size_t TWO_KILOBYTEs = 2 * ONE_KILOBYTE;
    inline constexpr size_t FOUR_KILOBYTES = 4 * ONE_KILOBYTE;
    inline constexpr size_t EIGHT_KILOBYTES = 8 * ONE_KILOBYTE;
    inline constexpr size_t SIXTEEN_KILOBYTES = 16 * ONE_KILOBYTE;
    inline constexpr size_t THIRTY_TWO_KILOBYTES = 32 * ONE_KILOBYTE;
    inline constexpr size_t SIXTY_FOUR_KILOBYTES = 64 * ONE_KILOBYTE;
    inline constexpr size_t ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES = 128 * ONE_KILOBYTE;
    inline constexpr size_t TWO_HUNDRED_FIFTY_SIX_KILOBYTES = 256 * ONE_KILOBYTE;
    inline constexpr size_t ONE_MEGABYTE   = ONE_KILOBYTE * ONE_KILOBYTE;
    inline constexpr size_t TWO_MEGABYTES  = 2 * ONE_MEGABYTE;

    inline constexpr int TEN_MILLISECONDS = 10000;
    inline constexpr int ONE_HUNDRED_MILLISECONDS = 100000;
}

namespace BufferPoolConfig {
    /**
     * @brief 
     */
    inline constexpr int INITIAL_POOL_SIZE = 4;

    /**
     * @brief 
     */
    inline constexpr int MAX_POOL_SIZE = 8;

    /**
     * @brief 
     */
    static constexpr size_t BUFFER_PADDING = 128;
}

namespace UsbConfig {
    /**
     * @brief How long (in ms) we will wait for the USB hardware to respond before assuming it disconnected.
     */
    inline constexpr unsigned int USB_TIMEOUT = 1000;

    /**
     * @brief How long (in ms) we will wait for the USB hardware to respond before assuming it disconnected.
     */
    inline constexpr unsigned int SHUTDOWN_WAIT_TIMEOUT = 50;

    inline constexpr size_t BULK_TRANSFER_COUNT = 4;

    inline constexpr size_t BULK_TRANSFER_SIZE = Units::SIXTEEN_KILOBYTES;

    inline constexpr size_t DMA_BUFFER_SIZE = BULK_TRANSFER_COUNT * BULK_TRANSFER_SIZE;
}

namespace WebServerConfig {
    /**
     * @brief The absolute maximum number of web browsers allowed to watch the live feed simultaneously.
     */
    inline constexpr size_t MAX_CLIENTS = 16;

    /**
     * @brief The size of the fast temporary memory stack used to build network text headers.
     */
    inline constexpr size_t HEADER_BUFFER_SIZE = 128;

    /**
     * @brief 
     */
    inline constexpr int TIMER_FALLTHROUGH = 0;

    /**
     * @brief 
     */
    inline constexpr int TIMER_INTERVAL_MS = 15;

    /**
     * @brief 
     */
    inline const size_t MAX_OUTGOING_CLIENT_BUFFER_SIZE = Units::TWO_MEGABYTES;
}

namespace HttpHeaders {
	/**
     * @brief The HTTP label that separates one picture from the next.
     * @details Web browsers need this exact text boundary to know when one JPEG ends 
     * and the next one begins, which creates the illusion of smooth video.
     */
    inline constexpr std::string_view MJPEG_CHUNK_PREFIX = 
        "--mjpegstream\r\nContent-Type: image/jpeg\r\nContent-Length: ";
    
    /** 
     * @brief The suffix for MJPEG chunks
     */
    inline constexpr std::string_view MJPEG_CHUNK_SUFFIX = "\r\n\r\n";
}

namespace uWS { template <bool SSL> struct HttpResponse; }

namespace Web {
    struct ViewerState {
        uWS::HttpResponse<false>* res{};
        uint32_t lastSentFrameId{0};
        bool isClosed{false};

        bool isLagging{false};
        uint32_t lagStartFrameId{0};
    };
}

namespace Arguments {
	enum class ParseResult : std::uint8_t {
		Success,
		HelpRequested,
		Error
	};
}

namespace USB {
    enum class TransferStatus :std::uint8_t {
        Completed,
        Disconnected,
        Error
    };

    inline constexpr size_t USB_PACKET_HEADER_SIZE = sizeof(USB::UsbPacketHeader);
    inline constexpr size_t USB_PAYLOAD_HEADER_SIZE = sizeof(USB::UsbPayloadHeader);
    inline constexpr size_t TOTAL_USB_HEADER_SIZE = USB_PACKET_HEADER_SIZE + USB_PAYLOAD_HEADER_SIZE;
}
