#pragma once

#include <sys/socket.h>
#include <cerrno>
#include <cstdint>
#include <iostream>
#include <vector>
#include "server_constants.hpp"

/** 
 * @brief Structure representing the state of a client connection
 */
struct ClientState {
    static constexpr size_t READ_BUFFER_SIZE = ServerConstants::FOUR_KILOBYTES;
    static constexpr size_t ENGINES_EXPECTED_FRAME_MAX = ServerConstants::FORTY_KILOBYTES; 

    /** 
     * @brief The file descriptor for the client connection
     */
    int fileDescriptor = -1;

    /** 
     * @brief Flag indicating if the client is active
     */
    bool isActive = false;

    /** 
     * @brief The buffer for reading data from the client
     */
    std::vector<char> readBuffer;

    /** 
     * @brief The length of the data in the read buffer
     */
    size_t readBufferLen = 0;
    
    /** 
     * @brief The buffer for writing data to the client
     */
    std::vector<uint8_t> outbox;

    /** 
     * @brief The length of the data in the outbox
     */
    size_t outboxLen = 0;

    /** 
     * @brief The offset of the data in the outbox
     */
    size_t outboxOffset = 0;
    
    /** 
     * @brief The ID of the frame that has been sent
     */
    uint32_t sentFrameId = 0;

    /** 
     * @brief Flag indicating if the client is streaming
     */
    bool isStreaming = false;

    /** 
     * @brief Flag indicating if the client connection should be closed after writing
     */
    bool closeAfterWrite = false;

    /**
     * @brief Default constructor allocating lean heap buffers
     */
    ClientState() {
        readBuffer.resize(READ_BUFFER_SIZE);
        outbox.reserve(ENGINES_EXPECTED_FRAME_MAX);
    }

    /** 
     * @brief Reset the client state with a new file descriptor
     * @param descriptor The file descriptor for the client connection
     */
    void reset(int descriptor) {
        fileDescriptor = descriptor;
        isActive = true;
        readBufferLen = 0;
        outboxLen = 0;
        outboxOffset = 0;
        sentFrameId = 0;
        isStreaming = false;
        closeAfterWrite = false;
        
        // Clear out old frame bytes but maintain the internal vector allocation capacity
        outbox.clear(); 
    }

    void queueData(const uint8_t* data, size_t size) {
        if (!isActive || data == nullptr || size == 0) {
            return; 
        }

        if (outboxLen == 0) {
            ssize_t sent = send(fileDescriptor, data, size, MSG_NOSIGNAL);
            
            if (sent >= 0) {
                size_t bytesSent = static_cast<size_t>(sent);
                if (bytesSent == size) { 
                    return;
                }

                size_t remaining = size - bytesSent;

                outbox.assign(data + bytesSent, data + size);
                outboxLen = remaining;
                outboxOffset = 0;
                return;
            } 

            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                // Hard error: Flag connection as dead for poll() cleanup. No deadlock!
                isActive = false;
                return;
            }
        }

        if (outboxLen + size > ClientState::ENGINES_EXPECTED_FRAME_MAX * 3) {
            std::cerr << "[Network Core] Outbox overflow on FD " << fileDescriptor << ". Evicting \n";
            isActive = false;
            return;
        }

        outbox.insert(outbox.end(), data, data + size);
        outboxLen += size;
    }
};
