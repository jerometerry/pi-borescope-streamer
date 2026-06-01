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
    /** @brief Enum representing the status of a write operation.
     */
    enum class WriteStatus : uint8_t {
        Flushed,
        Pending,
        ClosedOrError
    };

    /** @brief Static constants for buffer sizes.
     */
    static constexpr size_t READ_BUFFER_SIZE = ServerConstants::FOUR_KILOBYTES;

    /** @brief Static constant for the maximum expected frame size from engines.
     */
    static constexpr size_t ENGINES_EXPECTED_FRAME_MAX = ServerConstants::FORTY_KILOBYTES; 

    /** @brief Static constant for the maximum capacity of the output box.
     */
    static constexpr size_t MAX_OUTBOX_CAPACITY = ENGINES_EXPECTED_FRAME_MAX * 3;

    /** @brief Default constructor.
     */
    ClientConnection() {
        m_readBuffer.resize(READ_BUFFER_SIZE);
        m_outbox.reserve(MAX_OUTBOX_CAPACITY);
    }

    /** @brief Deleted copy constructor.
     */
    ClientConnection(const ClientConnection&) = delete;

    /** @brief Deleted assignment operator.
     */
    ClientConnection& operator=(const ClientConnection&) = delete;

    /** @brief Default move constructor.
     */
    ClientConnection(ClientConnection&&) noexcept = default;

    /** @brief Default move assignment operator.
     */
    ClientConnection& operator=(ClientConnection&&) noexcept = default;

    /** @brief Get the file descriptor for the client connection.
     *  @return The file descriptor.
     */
    [[nodiscard]] int fd() const noexcept { 
        return m_fileDescriptor; 
    }

    /** @brief Check if the client connection is active.
     *  @return True if active, false otherwise.
     */
    [[nodiscard]] bool isActive() const noexcept { 
        return m_isActive; 
    }

    /** @brief Check if the client connection is streaming.
     *  @return True if streaming, false otherwise.
     */
    [[nodiscard]] bool isStreaming() const noexcept { 
        return m_isStreaming; 
    }

    /** @brief Check if the client connection should close after writing.
     *  @return True if it should close, false otherwise.
     */
    [[nodiscard]] bool closeAfterWrite() const noexcept { 
        return m_closeAfterWrite; 
    }

    /** @brief Get the ID of the last sent frame.
     *  @return The frame ID.
     */
    [[nodiscard]] uint32_t sentFrameId() const noexcept { 
        return m_sentFrameId; 
    }

    /** @brief Check if the output box is empty.
     *  @return True if empty, false otherwise.
     */
    [[nodiscard]] bool isOutboxEmpty() const noexcept { 
        return m_outbox.empty(); 
    }

    /** @brief Get the size of the output box.
     *  @return The size of the output box.
     */
    [[nodiscard]] size_t outboxSize() const noexcept { 
        return m_outbox.size(); 
    }

    /** @brief Get a pointer to the read buffer.
     *  @return A pointer to the read buffer.
     */
    [[nodiscard]] const char* readData() const noexcept { 
        return m_readBuffer.data(); 
    }

    /** @brief Get the length of the data in the read buffer.
     *  @return The length of the data in the read buffer.
     */
    [[nodiscard]] size_t readLen() const noexcept { 
        return m_readBufferLen; 
    }

    /** @brief Set the streaming status of the client connection.
     *  @param streaming The streaming status.
     */
    void setStreaming(bool streaming) noexcept { 
        m_isStreaming = streaming; 
    }

    /** @brief Set the close-after-write status of the client connection.
     *  @param close The close-after-write status.
     */
    void setCloseAfterWrite(bool close) noexcept { 
        m_closeAfterWrite = close; 
    }

    /** @brief Set the ID of the last sent frame.
     *  @param frameId The frame ID.
     */
    void setSentFrameId(uint32_t frameId) noexcept { 
        m_sentFrameId = frameId; 
    }

    /** @brief Reset the read buffer.
     *  @return True if successful, false otherwise.
     */
    void resetReadBuffer() noexcept { 
        m_readBufferLen = 0; 
    }

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
        m_outbox.clear();
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
            m_outbox.clear();

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
            return false;
        }
        std::memcpy(m_readBuffer.data() + m_readBufferLen, data, size);
        m_readBufferLen += size;
        return true;
    }

    /**
    * @brief Automatically generates a content-length header, structures the HTTP frame, and queues the payload.
    * @param headerPrefix The HTTP header string leading up to the content-length (e.g., HTTP_OK_HTML_HDR).
    * @param payload The raw trailing content payload to transmit.
    * @param headerSuffix The trailing headers block (e.e.g HTTP_HDR_END).
    */
    template <typename T>
    void queueResponse(std::string_view headerPrefix,
                       const T& payload, 
                       std::string_view headerSuffix) {
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

    /** @brief Queue data for transmission.
     *  @param data Pointer to the data to queue.
     *  @param size Size of the data to queue.
     */
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

    /** @brief Queue a string view for transmission.
     *  @param data The string view to queue.
     */
    void queueData(std::string_view data) {
        queueData(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    }

    /** @brief Queue a container of data for transmission.
     *  @param container The container to queue.
     */
    template <typename T>
    void queueData(const T& container) {
        queueData(reinterpret_cast<const uint8_t*>(container.data()), container.size());
    }

    /** @brief Queue data for transmission.
     *  @param bufStart Pointer to the start of the buffer.
     *  @param result The result of the conversion.
     */
    void queueData(const char* bufStart, const std::to_chars_result& result) {
        if (result.ec == std::errc{}) [[likely]] {
            queueData(std::string_view(bufStart, result.ptr - bufStart));
        }
    }

private:
    /** @brief The file descriptor for the client connection.
     */
    int m_fileDescriptor = -1;

    /** @brief Indicates if the connection is active.
     */
    bool m_isActive = false;

    /** @brief Indicates if the connection is in streaming mode.
     */
    bool m_isStreaming = false;

    /** @brief Indicates if the connection should be closed after writing.
     */
    bool m_closeAfterWrite = false;
    
    /** @brief The buffer for reading incoming data.
     */
    std::vector<char> m_readBuffer;

    /** @brief The length of the data in the read buffer.
     */
    size_t m_readBufferLen = 0;
    
    /** @brief The buffer for outgoing data.
     */
    std::vector<uint8_t> m_outbox;

    /** @brief The offset of the data in the outbox.
     */
    size_t m_outboxOffset = 0;

    /** @brief The ID of the last sent frame.
     */
    uint32_t m_sentFrameId = 0;
};
