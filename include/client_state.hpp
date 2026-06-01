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

        // 1. Fast-Path: If nothing is currently waiting in the queue, try a direct kernel socket push
        if (outbox.empty()) {
            ssize_t sent = send(fileDescriptor, data, size, MSG_NOSIGNAL);
            
            if (sent >= 0) {
                size_t bytesSent = static_cast<size_t>(sent);
                if (bytesSent == size) { 
                    return; // 100% data delivered cleanly with zero heap memory copying!
                }
                
                // Handle partial socket writes safely using standard vector initialization
                outbox.assign(data + bytesSent, data + size);
                outboxOffset = 0;
                return;
            } 

            // Handle temporary blocking signals or hard network socket drops
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                isActive = false; // Mark client for immediate loop eviction
                return;
            }
        }

        // 2. Slow-Path: Protect the host from memory exhaustion by slow network clients
        if (outbox.size() + size > ClientState::ENGINES_EXPECTED_FRAME_MAX * 3) {
            std::cerr << "[Network Core] Outbox memory capacity overflow on FD " << fileDescriptor << ". Evicting client.\n";
            isActive = false;
            return;
        }

        // Append raw data to our heap-allocated queue cleanly using standard vector mechanics
        outbox.insert(outbox.end(), data, data + size);
    }

};
