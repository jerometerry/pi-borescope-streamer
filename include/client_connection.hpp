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
 * @brief The dedicated viewing slot and network "outbox" for a single connected user.
 * @details When someone opens the camera stream on their phone or laptop, the server 
 * assigns them one of these slots. This class holds the memory used to read what the 
 * viewer is asking for (like `/stream` or `/snapshot`) and the "outbox" where we queue 
 * up video pictures to send back to them over the Wi-Fi.
 *
 * To keep the server lightning fast and prevent memory crashes, these slots are never 
 * destroyed or recreated. When a viewer leaves, the slot is simply marked "empty" and 
 * its memory is immediately recycled for the next person who connects.
 */
class ClientConnection {
public:
    /**
     * @brief The status report after attempting to push video data out over the Wi-Fi.
     */
    enum class WriteStatus : uint8_t {
        /** 
         * @brief All data was successfully pushed to the viewer. 
         */
        Flushed,
        /** 
         * @brief The viewer's Wi-Fi is slow; we pushed what we could, but need to try again later. 
         */
        Pending,
        /** 
         * @brief The viewer disconnected or the network cable was unplugged. 
         */
        ClosedOrError
    };

    /**
     * @brief The maximum size of an incoming web request from a browser.
     */
    static constexpr size_t READ_BUFFER_SIZE = ServerConstants::FOUR_KILOBYTES;

    /**
     * @brief The largest single JPEG picture we expect the camera decoder to produce.
     */
    static constexpr size_t ENGINES_EXPECTED_FRAME_MAX = ServerConstants::FORTY_KILOBYTES; 

    /**
     * @brief The lag limit. The maximum amount of unsent data we will hold for a viewer.
     * @details If a viewer's Wi-Fi is terrible and they fall this far behind the live 
     * stream, the server will kick them out to prevent their "outbox" from eating all 
     * the Raspberry Pi's memory.
     */
    static constexpr size_t MAX_OUTBOX_CAPACITY = ENGINES_EXPECTED_FRAME_MAX * 3;

    /**
     * @brief Construct the slot and permanently reserve its memory.
     * @details The memory for the outbox and read buffer is reserved immediately on startup 
     * and never shrinks or grows, completely eliminating dynamic allocation during the live stream.
     */
    ClientConnection() {
        m_readBuffer.resize(READ_BUFFER_SIZE);
        m_outbox.reserve(MAX_OUTBOX_CAPACITY);
    }

    /**
     * @brief Prevent copying to ensure two viewers don't accidentally share an outbox.
     */
    ClientConnection(const ClientConnection&) = delete;

    /**
     * @brief Prevent assignment copying to enforce unique ownership of the network buffers.
     */
    ClientConnection& operator=(const ClientConnection&) = delete;

    /**
     * @brief Default move constructor.
     */
    ClientConnection(ClientConnection&&) noexcept = default;

    /**
     * @brief Default move assignment operator.
     */
    ClientConnection& operator=(ClientConnection&&) noexcept = default;

    /**
     * @brief Get the operating system's unique ticket number for this network connection.
     * @return The raw file descriptor used to talk to the Wi-Fi hardware.
     */
    [[nodiscard]] int fd() const noexcept { 
        return m_fileDescriptor; 
    }

    /**
     * @brief Check if someone is currently parked in this viewing slot.
     * @return True if a viewer is connected and active.
     */
    [[nodiscard]] bool isActive() const noexcept { 
        return m_isActive; 
    }

    /**
     * @brief Check if this specific viewer requested the continuous live video feed.
     * @return True if they are watching the live stream, false if they just loaded a static webpage.
     */
    [[nodiscard]] bool isStreaming() const noexcept { 
        return m_isStreaming; 
    }

    /**
     * @brief Check if we should hang up on this viewer as soon as their outbox is empty.
     * @return True if the server intends to close the connection after the current message is sent.
     */
    [[nodiscard]] bool closeAfterWrite() const noexcept { 
        return m_closeAfterWrite; 
    }

    /**
     * @brief Get the ID of the last video picture successfully queued for this viewer.
     * @return The sequence number, ensuring we don't accidentally send them the same picture twice.
     */
    [[nodiscard]] uint32_t sentFrameId() const noexcept { 
        return m_sentFrameId; 
    }

    /**
     * @brief Check if we have successfully sent all waiting data to this viewer.
     * @return True if their outbox is completely empty.
     */
    [[nodiscard]] bool isOutboxEmpty() const noexcept { 
        return m_outbox.empty(); 
    }

    /**
     * @brief Check exactly how many bytes are waiting to be sent to this viewer.
     * @return The current size of the backlog in their outbox.
     */
    [[nodiscard]] size_t outboxSize() const noexcept { 
        return m_outbox.size(); 
    }

    /**
     * @brief Look at the raw text this viewer's web browser sent to us.
     * @return A pointer to their incoming request.
     */
    [[nodiscard]] const char* readData() const noexcept { 
        return m_readBuffer.data(); 
    }

    /**
     * @brief Get the exact length of the viewer's web request.
     * @return The number of valid bytes in the read buffer.
     */
    [[nodiscard]] size_t readLen() const noexcept { 
        return m_readBufferLen; 
    }

    /**
     * @brief Mark this viewer as someone who wants continuous video updates.
     * @param streaming True to subscribe them to the live feed.
     */
    void setStreaming(bool streaming) noexcept { 
        m_isStreaming = streaming; 
    }

    /**
     * @brief Instruct the slot to automatically kick the viewer once the outbox is emptied.
     * @param close True to flag the connection for teardown.
     */
    void setCloseAfterWrite(bool close) noexcept { 
        m_closeAfterWrite = close; 
    }

    /**
     * @brief Update the bookmark tracking which video picture this viewer saw last.
     * @param frameId The sequence number of the picture.
     */
    void setSentFrameId(uint32_t frameId) noexcept { 
        m_sentFrameId = frameId; 
    }

    /**
     * @brief Clear the incoming text buffer after we've finished reading the viewer's request.
     */
    void resetReadBuffer() noexcept { 
        m_readBufferLen = 0; 
    }

    /**
     * @brief Welcome a new viewer into this slot and prepare it for use.
     * @details Resets all tracking data to zero for the new occupant. Because it keeps 
     * the internal buffer capacities completely intact, welcoming a new viewer requires 
     * zero memory allocation from the operating system.
     * @param descriptor The new network connection assigned to this slot.
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
     * @brief Instantly mark the viewer dead and throw away their unsent data.
     * @details This is the backpressure release valve. If a viewer disconnects or lags 
     * too hard, this clears their outbox and marks the slot as empty so the memory can 
     * be safely reused by the next person.
     */
    void evict() noexcept {
        m_isActive = false;
        m_fileDescriptor = -1;
        m_outbox.clear();
    }

    /**
     * @brief Attempt to push the waiting outbox data over the physical Wi-Fi hardware.
     * @details Sends as many bytes as the operating system's network buffer can currently 
     * handle. If the network is congested, it remembers exactly where it left off and 
     * safely pauses until the next server tick.
     * @return A WriteStatus indicating if we finished, paused, or if the connection broke.
     */
    [[nodiscard]] WriteStatus flushOutbox() noexcept {
        if (!m_isActive) { 
            return WriteStatus::ClosedOrError; 
        }
        if (m_outbox.empty()) {
            return WriteStatus::Flushed;
        }

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
     * @brief Save incoming text from the viewer's browser into our read buffer.
     * @param data The raw text arriving over the network.
     * @param size How many characters arrived.
     * @return True if the text fit safely, false if the viewer tried to send us too much data (overflow).
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
     * @brief The packaging department. Wraps raw data in standard web browser formatting.
     * @details Browsers refuse to display data unless they are told exactly how big it is first. 
     * This function automatically calculates the size of the payload, writes the required `Content-Length` 
     * label, and stacks the header, size, and payload perfectly into the viewer's outbox.
     * @param headerPrefix The HTTP rules leading up to the size declaration (e.g., "Content-Type: text/html").
     * @param payload The actual data (like an HTML webpage or a JPEG image) we are sending.
     * @param headerSuffix The blank line required to tell the browser the rules are finished.
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
     * @brief Package and queue a standard HTML webpage.
     * @param payload The text of the webpage to send.
     */
    template <typename T>
    void queueHttpOkResponse(const T& payload) {
        queueResponse(
            ServerConstants::HTTP_OK_HTML_HDR, 
            payload, 
            ServerConstants::HTTP_HDR_END);
    }

    /**
     * @brief Package and queue a single, standalone JPEG picture (like a snapshot).
     * @param payload The raw bytes of the JPEG image.
     */
    template <typename T>
    void queueJpegOkResponse(const T& payload) {
        queueResponse(
            ServerConstants::HTTP_OK_JPEG_HDR,
            payload, 
            ServerConstants::HTTP_HDR_END
        );
    }

    /**
     * @brief Drop a block of raw bytes directly into the viewer's outbox.
     * @details If the outbox is currently empty, it skips the line and tries to hand the 
     * bytes directly to the Wi-Fi hardware immediately. If the hardware is busy, it safely 
     * puts the rest in the outbox. It enforces the lag limit by kicking the viewer if the 
     * outbox overflows.
     * @param data Pointer to the bytes to send.
     * @param size How many bytes to send.
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

    /**
     * @brief Drop a string of text into the viewer's outbox.
     * @param data The text message to send.
     */
    void queueData(std::string_view data) {
        queueData(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    }

    /**
     * @brief Drop an entire container of data (like a `std::vector`) into the viewer's outbox.
     * @param container The object holding the data.
     */
    template <typename T>
    void queueData(const T& container) {
        queueData(reinterpret_cast<const uint8_t*>(container.data()), container.size());
    }

    /**
     * @brief A helper to cleanly drop the result of a fast number-to-text conversion into the outbox.
     * @param bufStart Where the converted text begins in memory.
     * @param result Information about where the text ends.
     */
    void queueData(const char* bufStart, const std::to_chars_result& result) {
        if (result.ec == std::errc{}) [[likely]] {
            queueData(std::string_view(bufStart, result.ptr - bufStart));
        }
    }

private:
    /**
     * @brief The operating system's ticket number for this specific Wi-Fi connection.
     */
    int m_fileDescriptor = -1;

    /**
     * @brief True if someone is currently parked in this viewing slot.
     */
    bool m_isActive = false;

    /**
     * @brief True if the occupant is subscribed to the live video feed.
     */
    bool m_isStreaming = false;

    /**
     * @brief True if we plan to kick the occupant out once their outbox is completely emptied.
     */
    bool m_closeAfterWrite = false;
    
    /**
     * @brief The inbox used to read what the viewer's web browser is asking us to do.
     */
    std::vector<char> m_readBuffer;

    /**
     * @brief A bookmark tracking exactly how many valid characters are currently in the inbox.
     */
    size_t m_readBufferLen = 0;
    
    /**
     * @brief The outbox containing all the video pictures waiting to be sent over the Wi-Fi.
     */
    std::vector<uint8_t> m_outbox;

    /**
     * @brief A bookmark tracking our progress if we only managed to send part of the outbox data.
     */
    size_t m_outboxOffset = 0;

    /**
     * @brief Tracks the ID of the last video picture sent, preventing us from sending duplicates.
     */
    uint32_t m_sentFrameId = 0;
};