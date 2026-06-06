#pragma once

#include <atomic>
#include <bit>
#include <concepts>
#include <cstdint>
#include <vector>

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
    struct PooledFrame {
        std::vector<uint8_t> data_;
        std::atomic<int> refCount_{0};

        void (*returnCallback)(void* context, PooledFrame* frame){nullptr};
        void* poolContext{nullptr};
        
        void release() {
            if (refCount_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                if (returnCallback && poolContext) {
                    returnCallback(poolContext, this);
                }
            }
        }

        void clear() {
            data_.clear();
        }

        bool empty() const {
            return data_.empty();
        }

        uint8_t front() const {
            return data_.front();
        }

        size_t size() const {
            return data_.size();
        }

        std::vector<uint8_t>& data() {
            return data_;
        }

        void insert(const std::vector<uint8_t>& data) {
            data_.insert(data_.begin(), data.begin(), data.end());
        }
    };

    /**
     * @brief A zero-allocation replacement for std::shared_ptr.
     */
    class FramePtr {
    public:
        FramePtr() = default;
        
        explicit FramePtr(PooledFrame* frame) : frame_(frame) {
            if (frame_) {
                frame_->refCount_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        
        ~FramePtr() {
            if (frame_) { 
                frame_->release();
            }
        }

        FramePtr(FramePtr&& other) noexcept : frame_(other.frame_) {
            other.frame_ = nullptr;
        }
        
        FramePtr& operator=(FramePtr&& other) noexcept {
            if (this != &other) {
                if (frame_) frame_->release();
                frame_ = other.frame_;
                other.frame_ = nullptr;
            }
            return *this;
        }

        FramePtr(const FramePtr& other) : frame_(other.frame_) {
            if (frame_) {
                frame_->refCount_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        
        FramePtr& operator=(const FramePtr& other) {
            if (this != &other) {
                if (frame_) { 
                    frame_->release();
                }
                frame_ = other.frame_;
                if (frame_) { 
                    frame_->refCount_.fetch_add(1, std::memory_order_relaxed);
                }
            }
            return *this;
        }

        PooledFrame* get() const { 
            return frame_; 
        }

        PooledFrame* operator->() const { 
            return frame_; 
        }

        explicit operator bool() const { 
            return frame_ != nullptr; 
        }

    private:
        PooledFrame* frame_{nullptr};
    };

    enum class TransferStatus :std::uint8_t {
        Completed,
        Disconnected,
        Error
    };

    /**
    * @brief Single-byte bypass for wire translation.
    * @details A single byte (8 bits) has no endianness. This overload ensures that 
    * asking to translate a single byte safely returns the exact same value without 
    * invoking the template logic.
    * @param val The raw byte directly from the USB wire.
    * @return The exact same byte.
    */
    constexpr uint8_t wireToHost(uint8_t val) noexcept {
        return val;
    }

    /**
    * @brief Safely translate multi-byte numbers coming off the USB wire into native CPU numbers.
    * @details The physical camera always sends its numbers in "Little-Endian" format. 
    * If you are running this on a standard Raspberry Pi, your computer already speaks 
    * this language, and this function completely disappears when compiled, costing zero 
    * performance. 
    * 
    * However, if you compile this on a system that reads numbers backward (Big-Endian), 
    * it will automatically flip the bytes so the hardware data isn't misinterpreted as 
    * corrupted garbage.
    * @param val The raw number directly from the USB wire.
    * @return The safely formatted native host number.
    */
    template <std::integral T>
    constexpr T wireToHost(T val) noexcept {
        if constexpr (std::endian::native == std::endian::big) {
            return std::byteswap(val);
        }
        return val;
    }

    /**
    * @brief Single-byte bypass for wire packaging.
    * @details A single byte has no endianness, so this safely returns the value exactly as-is.
    * @param val The native byte from our software.
    * @return The exact same byte.
    */
    constexpr uint8_t hostToWire(uint8_t val) noexcept {
        return val;
    }

    /**
    * @brief Safely package multi-byte native CPU numbers before sending them out over the USB wire.
    * @details Formats numbers into the strict byte order the camera hardware expects.
    * @param val The native host number from our software.
    * @return The number formatted for the USB wire.
    */
    template <std::integral T>
    constexpr T hostToWire(T val) noexcept {
        if constexpr (std::endian::native == std::endian::big) {
            return std::byteswap(val);
        }
        return val;
    }

    /**
    * @brief The outer "shipping envelope" that safely transports data across the USB cable.
    * @details In network terms, this is the transport layer. When the camera fires data down 
    * the wire, it places every chunk of video into this exact 5-byte envelope. We read this 
    * outer header first to know exactly how many bytes are inside, ensuring we never read 
    * out of bounds and crash the server. 
    * 
    * Once we verify this envelope is valid and safe to open, we strip it away to reveal 
    * the actual inner payload (which begins with the PayloadHeader).
    */
    struct [[gnu::packed]] PacketHeader {

        /**
        * @brief The raw, un-translated secret code identifying this as a valid camera chunk.
        */
        uint16_t leHeader;

        /**
        * @brief Which physical camera lens this data came from (used if the endoscope has multiple lenses).
        */
        uint8_t leCameraId;

        /**
        * @brief The raw, un-translated size of the video payload inside this envelope.
        */
        uint16_t leLength;

        /**
        * @brief Read the header verification code safely.
        * @return The translated verification code, ready for our software to check.
        */
        constexpr uint16_t getHeader() const noexcept { 
            return wireToHost(leHeader); 
        }

        /**
        * @brief Write the header verification code safely.
        * @param val The code to package for the camera.
        */
        constexpr void setHeader(uint16_t val) noexcept { 
            leHeader = hostToWire(val); 
        }

        /**
        * @brief Get the ID of the camera lens that generated this chunk.
        * @return The safe, translated camera ID.
        */
        constexpr uint8_t getCameraId() const noexcept { 
            return wireToHost(leCameraId); 
        }

        /**
        * @brief Set the ID of the camera lens generating this chunk.
        * @param val The camera ID.
        */
        constexpr void setCameraId(uint8_t val) noexcept { 
            leCameraId = hostToWire(val); 
        }

        /**
        * @brief Check exactly how many bytes of video data are enclosed in this envelope.
        * @return The safe, translated length of the inner payload.
        */
        constexpr uint16_t getLength() const noexcept { 
            return wireToHost(leLength); 
        }

        /**
        * @brief Set the length of the data chunk before sending it.
        * @param val The length in bytes.
        */
        constexpr void setLength(uint16_t val) noexcept { 
            leLength = hostToWire(val); 
        }
    };

    /**
    * @brief The internal assembly instructions for the actual camera payload.
    * @details When you strip away the outer USB packet, you are left with the raw camera payload. 
    * Because a single JPEG picture is too large to fit in one transfer, the camera chops it up. 
    * This 7-byte header sits at the front of the inner payload, providing the sequence number 
    * (`frameId`) needed to stitch the picture back together in the correct order.
    * 
    * Rather than creating a separate data channel for the physical hardware sensors, the camera's 
    * engineers cleverly used the remaining bytes in this header to piggyback the gravity sensor and 
    * button state alongside the video data.
    */
    struct [[gnu::packed]] PayloadHeader {

        /**
        * @brief A rolling counter that helps us stitch chunks together into a full picture.
        */
        uint8_t leFrameId;

        /**
        * @brief Identifies which lens is active on dual-lens endoscopes.
        */
        uint8_t leCameraNumber;

        /**
        * @brief A densely packed byte where each bit represents a yes/no switch (like a button press).
        */
        uint8_t leFlags;

        /**
        * @brief The raw, un-translated orientation data piggybacked from the camera's gyroscope.
        */
        uint32_t leGravitySensor;

        /**
        * @brief Get the sequence ID of the frame this chunk belongs to.
        * @return The safe, translated frame ID.
        */
        constexpr uint8_t getFrameId() const noexcept { 
            return wireToHost(leFrameId); 
        }

        /**
        * @brief Set the sequence ID for the frame this chunk belongs to.
        * @param val The frame ID.
        */
        constexpr void setFrameId(uint8_t val) noexcept { 
            leFrameId = hostToWire(val); 
        }

        /**
        * @brief Get the internal camera lens number.
        * @return The safe, translated camera number.
        */
        constexpr uint8_t getCameraNumber() const noexcept { 
            return wireToHost(leCameraNumber); 
        }

        /**
        * @brief Set the internal camera lens number.
        * @param val The camera number.
        */
        constexpr void setCameraNumber(uint8_t val) noexcept { 
            leCameraNumber = hostToWire(val); 
        }

        /**
        * @brief Get the raw hardware flags byte.
        * @return The safe, translated flags byte.
        */
        constexpr uint8_t getFlags() const noexcept { 
            return wireToHost(leFlags); 
        }

        /**
        * @brief Set the raw hardware flags byte.
        * @param val The flags byte to pack.
        */
        constexpr void setFlags(uint8_t val) noexcept { 
            leFlags = hostToWire(val); 
        }

        /**
        * @brief Get the camera's physical orientation safely.
        * @return The translated gyroscope reading.
        */
        constexpr uint32_t getGravitySensor() const noexcept { 
            return wireToHost(leGravitySensor); 
        }

        /**
        * @brief Set the camera's physical orientation safely.
        * @param val The gyroscope reading to pack.
        */
        constexpr void setGravitySensor(uint32_t val) noexcept { 
            leGravitySensor = hostToWire(val); 
        }

        /**
        * @brief Check if the camera handle actually has a gravity sensor installed.
        * @return True if the hardware supports orientation tracking.
        */
        constexpr bool hasGravitySensor() const noexcept { 
            return (getFlags() & 0x01) != 0; 
        }

        /**
        * @brief Toggle the flag indicating if the hardware has a gravity sensor.
        * @param hasGravitySensor True to turn the flag on, false to turn it off.
        */
        constexpr void setHasGravitySensor(bool hasGravitySensor) noexcept { 
            uint8_t current = getFlags();
            if (hasGravitySensor) { 
                current |= 0x01; 
            } else { 
                current &= ~0x01; 
            }
            setFlags(current);
        }

        /**
        * @brief Check if the user is actively pressing the physical button on the camera handle.
        * @return True if the button is currently held down.
        */
        constexpr bool isButtonPressed() const noexcept { 
            return (getFlags() & 0x02) != 0; 
        }

        /**
        * @brief Simulate or set the state of the physical camera button.
        * @param pressed True to mark the button as pressed.
        */
        constexpr void setButtonPressed(bool pressed) noexcept {
            uint8_t current = getFlags();
            if (pressed) {
                current |= 0x02;
            } else {
                current &= ~0x02;
            }
            setFlags(current);
        }

        /**
        * @brief Extract any extra unknown or reserved flags from the camera.
        * @return A clean byte containing only the reserved hardware flags.
        */
        constexpr uint8_t getOtherFlags() const noexcept { 
            return (getFlags() >> 2) & 0x3F; 
        }

        /**
        * @brief Set the extra unknown or reserved hardware flags.
        * @param val The flags to pack into the remaining bits.
        */
        constexpr void setOtherFlags(uint8_t val) noexcept {
            uint8_t current = getFlags();
            current &= 0x03;
            current |= ((val & 0x3F) << 2); 
            setFlags(current);
        }
    };

    inline constexpr size_t PACKET_HEADER_SIZE = sizeof(USB::PacketHeader);
    inline constexpr size_t PAYLOAD_HEADER_SIZE = sizeof(USB::PayloadHeader);
    inline constexpr size_t TOTAL_HEADER_SIZE = PACKET_HEADER_SIZE + PAYLOAD_HEADER_SIZE;
}

static_assert(
    sizeof(USB::PayloadHeader) == 7, 
    "PayloadHeader size must be exactly 7 bytes to match the hardware protocol!"
);