#pragma once
#include <cstdint>
#include <functional>
#include <span>

enum class UsbTransferStatus :std::uint8_t {
    Completed,
    Disconnected,
    Error
};

/**
 * @brief The pure logic engine for routing USB data.
 * @details Completely decoupled from libusb. It processes raw hardware states 
 * and routes data to the sink. 
 */
class UsbCaptureEngine {
public:
    explicit UsbCaptureEngine(std::function<void(std::span<const uint8_t>)> dataSink);

    /**
     * @brief Process an incoming hardware transfer from ANY hardware backend.
     * @return true if the hardware should request more data (resubmit), false if it should shut down.
     */
    bool processTransfer(UsbTransferStatus status, std::span<const uint8_t> payload);

private:
    std::function<void(std::span<const uint8_t>)> dataSink_;
};