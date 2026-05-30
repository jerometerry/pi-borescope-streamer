#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <vector>
#include <memory>

#include "mjpeg_stream.hpp"
#include "server_time.hpp"

class MjpegStreamTest : public ::testing::Test {
private:
    // Moved to 'private' to satisfy cppcoreguidelines-non-private-member-variables-in-classes
    ServerTime serverTime_{std::chrono::steady_clock::now()};
    std::unique_ptr<MjpegStream> stream_;

protected:
    void SetUp() override {
        stream_ = std::make_unique<MjpegStream>(serverTime_);

        // Artificially age the debounce timer into the past so the first 
        // simulated click in the tests isn't ignored as startup chatter.
        stream_->buttonLastSeen = std::chrono::steady_clock::now() - std::chrono::milliseconds(250);
    }

    // --- Proxy methods to access private MjpegStream members ---
    // Because MjpegStreamTest is the registered 'friend', these methods are 
    // legally allowed to touch the private internals of MjpegStream.

    void callBroadcastFrame(const std::vector<uint8_t>& frame) {
        stream_->broadcastFrame(frame);
    }

    void callHardwareButtonCallback() {
        stream_->hardwareButtonCallback();
    }

    const std::vector<uint8_t>& getFrameBuffer() const {
        return stream_->frameBuffer;
    }

    uint32_t getFrameId() const {
        return stream_->frameId;
    }

    const std::vector<uint8_t>& getSnapshotBuffer() const {
        return stream_->snapshotBuffer;
    }

    bool getSnapshotNextFrame() const {
        return stream_->snapshotNextFrame;
    }

    void setSnapshotNextFrame(bool value) {
        stream_->snapshotNextFrame = value;
    }
};

// 1. Tests the early return optimization
TEST_F(MjpegStreamTest, IgnoresEmptyFrames) {
    callBroadcastFrame({});
    
    EXPECT_TRUE(getFrameBuffer().empty());
    EXPECT_EQ(getFrameId(), 0); // ID should not increment
}

// 2. Tests the SOI (Start of Image) marker stripping algorithm
TEST_F(MjpegStreamTest, StripsLeadingGarbageBeforeJpegSoi) {
    std::vector<uint8_t> corruptedFrame = {0x01, 0x02, 0x03, 0xFF, 0xD8, 0xAA, 0xBB, 0xCC};
    
    callBroadcastFrame(corruptedFrame);

    std::vector<uint8_t> expectedPayload = {0xFF, 0xD8, 0xAA, 0xBB, 0xCC};
    
    EXPECT_EQ(getFrameBuffer(), expectedPayload);
    EXPECT_EQ(getFrameId(), 1); 
}

// 3. Tests the Snapshot pipeline integration
TEST_F(MjpegStreamTest, HardwareButtonTriggersSnapshotOnNextFrame) {
    callHardwareButtonCallback();
    EXPECT_TRUE(getSnapshotNextFrame());

    std::vector<uint8_t> nextVideoFrame = {0xFF, 0xD8, 0x11, 0x22, 0x33};
    callBroadcastFrame(nextVideoFrame);

    EXPECT_EQ(getSnapshotBuffer(), nextVideoFrame);
    EXPECT_FALSE(getSnapshotNextFrame());
}

// 4. Tests the chrono math to prevent switch chatter
TEST_F(MjpegStreamTest, DebouncesRapidHardwareButtonPresses) {
    callHardwareButtonCallback();
    EXPECT_TRUE(getSnapshotNextFrame());
    
    // Manually disarm it to test the cooldown window
    setSnapshotNextFrame(false);

    // Second press immediately after should be IGNORED
    callHardwareButtonCallback();
    EXPECT_FALSE(getSnapshotNextFrame()) << "Debounce failed: Allowed rapid double-press";

    // Sleep long enough to clear the 200ms BUTTON_DEBOUNCE_TIME_MS cooldown
    std::this_thread::sleep_for(std::chrono::milliseconds(210));

    // Third press should now be registered as a valid new click
    callHardwareButtonCallback();
    EXPECT_TRUE(getSnapshotNextFrame());
}