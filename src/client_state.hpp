#pragma once

#include "server_constants.hpp"
#include <array>
#include <cstdint>

struct ClientState {
    static constexpr size_t READ_BUFFER_SIZE = ServerConstants::FOUR_KILOBYTES;
    static constexpr size_t FRAME_HEADER_SAFETY_MARGIN = ServerConstants::FOUR_KILOBYTES;
    static constexpr size_t OUTBOX_BUFFER_SIZE = ServerConstants::TWO_MEGABYTES + FRAME_HEADER_SAFETY_MARGIN;

    int fileDescriptor = -1;
    bool isActive = false;

    std::array<char, READ_BUFFER_SIZE> readBuffer{};
    size_t readBufferLen = 0;
    
    std::array<uint8_t, OUTBOX_BUFFER_SIZE> outbox{};
    size_t outboxLen = 0;
    size_t outboxOffset = 0;
    
    uint32_t sentFrameId = 0;
    bool isStreaming = false;
    bool closeAfterWrite = false;

    void reset(int descriptor) {
        fileDescriptor = descriptor;
        isActive = true;
        readBufferLen = 0;
        outboxLen = 0;
        outboxOffset = 0;
        sentFrameId = 0;
        isStreaming = false;
        closeAfterWrite = false;
    }
};