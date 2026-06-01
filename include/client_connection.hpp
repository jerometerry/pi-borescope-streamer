#pragma once

#include <sys/socket.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>
#include <string_view>
#include <charconv>
#include "server_constants.hpp"

/** 
 * @brief Class managing the lifetime, network buffers, and state of a client connection
 */
class ClientConnection {
public:
    enum class WriteStatus : uint8_t {
        Flushed,
        Pending,
        ClosedOrError
    };

    static constexpr size_t READ_BUFFER_SIZE = ServerConstants::FOUR_KILOBYTES;
    static constexpr size_t ENGINES_EXPECTED_FRAME_MAX = ServerConstants::FORTY_KILOBYTES; 
    static constexpr size_t MAX_OUTBOX_CAPACITY = ENGINES_EXPECTED_FRAME_MAX * 3;

    ClientConnection() {
        m_readBuffer.resize(READ_BUFFER_SIZE);
        m_outbox.reserve(MAX_OUTBOX_CAPACITY);
    }

    // Disallow copying to prevent accidental buffer duplication overhead
    ClientConnection(const ClientConnection&) = delete;
    ClientConnection& operator=(const ClientConnection&) = delete;
    ClientConnection(ClientConnection&&) noexcept = default;
    ClientConnection& operator=(ClientConnection&&) noexcept = default;

    // --- State Accessors ---
    [[nodiscard]] int fd() const noexcept { return m_fileDescriptor; }
    [[nodiscard]] bool isActive() const noexcept { return m_isActive; }
    [[nodiscard]] bool isStreaming() const noexcept { return m_isStreaming; }
    [[nodiscard]] bool closeAfterWrite() const noexcept { return m_closeAfterWrite; }
    [[nodiscard]] uint32_t sentFrameId() const noexcept { return m_sentFrameId; }
    [[nodiscard]] bool isOutboxEmpty() const noexcept { return m_outbox.empty(); }
    [[nodiscard]] size_t outboxSize() const noexcept { return m_outbox.size(); }
    
    // Read buffer accessors for zero-copy string_view scanning
    [[nodiscard]] const char* readData() const noexcept { return m_readBuffer.data(); }
    [[nodiscard]] size_t readLen() const noexcept { return m_readBufferLen; }

    // --- Mutators ---
    void setStreaming(bool streaming) noexcept { m_isStreaming = streaming; }
    void setCloseAfterWrite(bool close) noexcept { m_closeAfterWrite = close; }
    void setSentFrameId(uint32_t frameId) noexcept { m_sentFrameId = frameId; }
    void resetReadBuffer() noexcept { m_readBufferLen = 0; }

    /** 
     * @brief Reset the client object state for a new incoming connection.
     * Keeps internal buffer capacities intact to avoid dynamic reallocations.
     */
    void activate(int descriptor) noexcept {
        m_fileDescriptor = descriptor;
        m_isActive = true;
        m_readBufferLen = 0;
        m_outboxOffset = 0;
        m_sentFrameId = 0;
        m_isStreaming = false;
        m_closeAfterWrite = false;
        m_outbox.clear(); // Shrinks size to 0, preserves pre-allocated capacity
    }

    /**
     * @brief Instantly mark a client dead and clear its data tracks without dumping capacity
     */
    void evict() noexcept {
        m_isActive = false;
        m_fileDescriptor = -1;
        m_outbox.clear();
    }

    [[nodiscard]] WriteStatus flushOutbox() noexcept {
        if (!m_isActive) return WriteStatus::ClosedOrError;
        if (m_outbox.empty()) return WriteStatus::Flushed;

        while (m_outboxOffset < m_outbox.size()) {
            size_t remaining = m_outbox.size() - m_outboxOffset;
            ssize_t sent = send(m_fileDescriptor, m_outbox.data() + m_outboxOffset, remaining, MSG_NOSIGNAL);

            if (sent > 0) {
                m_outboxOffset += static_cast<size_t>(sent);
            } else if (sent == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) { 
                    return WriteStatus::Pending; 
                }
                evict();
                return WriteStatus::ClosedOrError;
            }
        }

        if (m_outboxOffset == m_outbox.size()) {
            m_outboxOffset = 0;
            m_outbox.clear(); // Size goes to 0, capacity remains fully intact

            if (m_closeAfterWrite) {
                evict();
                return WriteStatus::ClosedOrError;
            }
        }

        return WriteStatus::Flushed;
    }

    /**
     * @brief Append data directly into the client read buffer (moved from handleRead)
     */
    bool appendToReadBuffer(const char* data, size_t size) noexcept {
        if (m_readBufferLen + size > m_readBuffer.size()) {
            return false; // Overflow safety trigger
        }
        std::memcpy(m_readBuffer.data() + m_readBufferLen, data, size);
        m_readBufferLen += size;
        return true;
    }

    /**
    * @brief Automatically generates a content-length header, structures the HTTP frame, and queues the payload.
    * @param headerPrefix The HTTP header string leading up to the content-length (e.g., HTTP_OK_HTML_HDR).
    * @param payload The raw trailing content payload to transmit.
    * @param headerSuffix The trailing headers block (defaults to HTTP_HDR_END).
    */
    template <typename T>
    void queueResponse(std::string_view headerPrefix,
                       const T& payload, 
                       std::string_view headerSuffix = ServerConstants::HTTP_HDR_END) {
        queueData(headerPrefix);

        char headerStackBuf[ServerConstants::STACK_BUF_SIZE]; 
        std::to_chars_result result = std::to_chars(
            headerStackBuf, 
            headerStackBuf + ServerConstants::STACK_BUF_SIZE, 
            payload.size()
        );
        queueData(headerStackBuf, result);
        queueData(headerSuffix);
        queueData(payload);
    }

    /**
    * @brief Automatically generates a content-length header, structures the HTTP frame, and queues the payload.
    * @param payload The raw trailing content payload to transmit.
    */
    template <typename T>
    void queueHttpOkResponse(const T& payload) {
        queueResponse(
            ServerConstants::HTTP_OK_HTML_HDR, 
            payload, 
            ServerConstants::HTTP_HDR_END);
    }

    /**
    * @brief Structures and queues an HTTP 200 OK image/jpeg response.
    */
    template <typename T>
    void queueJpegOkResponse(const T& payload) {
        queueResponse(
            ServerConstants::HTTP_OK_JPEG_HDR,
            payload, 
            ServerConstants::HTTP_HDR_END
        );
    }

    void queueData(const uint8_t* data, size_t size) {
        if (!m_isActive || data == nullptr || size == 0) {
            return; 
        }

        if (m_outbox.empty()) {
            ssize_t sent = send(m_fileDescriptor, data, size, MSG_NOSIGNAL);
            
            if (sent >= 0) {
                size_t bytesSent = static_cast<size_t>(sent);
                if (bytesSent == size) { 
                    return;
                }

                m_outbox.assign(data + bytesSent, data + size);
                m_outboxOffset = 0;
                return;
            } 

            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                m_isActive = false;
                return;
            }
        }

        if (m_outbox.size() + size > MAX_OUTBOX_CAPACITY) {
            std::cerr << "[Network Core] Outbox capacity overflow on FD " << m_fileDescriptor << ". Evicting.\n";
            evict();
            return;
        }

        m_outbox.insert(m_outbox.end(), data, data + size);
    }

    void queueData(std::string_view data) {
        queueData(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    }

    template <typename T>
    void queueData(const T& container) {
        queueData(reinterpret_cast<const uint8_t*>(container.data()), container.size());
    }

    void queueData(const char* bufStart, const std::to_chars_result& result) {
        if (result.ec == std::errc{}) [[likely]] {
            queueData(std::string_view(bufStart, result.ptr - bufStart));
        }
    }

private:
    int m_fileDescriptor = -1;
    bool m_isActive = false;
    bool m_isStreaming = false;
    bool m_closeAfterWrite = false;
    
    std::vector<char> m_readBuffer;
    size_t m_readBufferLen = 0;
    
    std::vector<uint8_t> m_outbox;
    size_t m_outboxOffset = 0;
    uint32_t m_sentFrameId = 0;
};
