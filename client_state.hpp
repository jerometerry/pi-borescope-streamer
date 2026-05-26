#pragma once

#include "server_constants.hpp"
#include <array>

struct ClientState {
    static constexpr size_t READ_BUFFER_SIZE = ServerConstants::FOUR_KILOBYTES;
    static constexpr size_t FRAME_HEADER_SAFETY_MARGIN = ServerConstants::FOUR_KILOBYTES;
    static constexpr size_t OUTBOX_BUFFER_SIZE = ServerConstants::TWO_MEGABYTES + FRAME_HEADER_SAFETY_MARGIN;

    int fd = -1;
    bool is_active = false;

    std::array<char, READ_BUFFER_SIZE> read_buffer;
    size_t read_buffer_len = 0;
    
    std::array<uint8_t, OUTBOX_BUFFER_SIZE> outbox;
    size_t outbox_len = 0;
    size_t outbox_offset = 0;
    
    uint32_t sent_frame_id = 0;
    bool is_streaming = false;
    bool close_after_write = false;

    void reset(int new_fd) {
        fd = new_fd;
        is_active = true;
        read_buffer_len = 0;
        outbox_len = 0;
        outbox_offset = 0;
        sent_frame_id = 0;
        is_streaming = false;
        close_after_write = false;
    }
};