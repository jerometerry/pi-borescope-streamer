#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <memory>
#include <vector>
#include "clock.hpp"
#include "mjpeg_stream.hpp"
#include "server_time.hpp"

class TestClock : public Clock {
public:
    std::chrono::steady_clock::time_point currentTime{std::chrono::steady_clock::now()};

    std::chrono::steady_clock::time_point now() const noexcept override {
        return currentTime;
    }

    void advance(std::chrono::milliseconds ms) {
        currentTime += ms;
    }
};

class MjpegStreamTest : public ::testing::Test {
private:
    TestClock clock_;
    ServerTime serverTime_{clock_, clock_.now()};
    std::unique_ptr<MjpegStream> stream_;

protected:
    void SetUp() override {
        stream_ = std::make_unique<MjpegStream>(serverTime_);

        stream_->buttonLastSeen = clock_.now() - std::chrono::milliseconds(250);
    }

    void advanceTime(std::chrono::milliseconds ms) {
        clock_.advance(ms);
    }

    void callBroadcastFrame(const std::vector<uint8_t>& frame) {
        stream_->broadcastFrame(frame);
    }

    void callHardwareButtonCallback() {
        stream_->hardwareButtonCallback();
    }

    void callCheckForButtonQuickPress() {
        stream_->checkForButtonQuickPress();
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

    bool getButtonIsDepressed() const {
        return stream_->buttonIsDepressed;
    }
};

TEST_F(MjpegStreamTest, IgnoresEmptyFrames) {
    callBroadcastFrame({});
    
    EXPECT_TRUE(getFrameBuffer().empty());
    EXPECT_EQ(getFrameId(), 0);
}

TEST_F(MjpegStreamTest, StripsLeadingGarbageBeforeJpegSoi) {
    std::vector<uint8_t> corruptedFrame = {0x01, 0x02, 0x03, 0xFF, 0xD8, 0xAA, 0xBB, 0xCC};
    
    callBroadcastFrame(corruptedFrame);

    std::vector<uint8_t> expectedPayload = {0xFF, 0xD8, 0xAA, 0xBB, 0xCC};
    
    EXPECT_EQ(getFrameBuffer(), expectedPayload);
    EXPECT_EQ(getFrameId(), 1); 
}

TEST_F(MjpegStreamTest, HardwareButtonTriggersSnapshotOnNextFrame) {
    EXPECT_FALSE(getButtonIsDepressed());
    
    callHardwareButtonCallback();
    EXPECT_TRUE(getButtonIsDepressed());

    advanceTime(std::chrono::milliseconds(250));

    callCheckForButtonQuickPress();
    EXPECT_TRUE(getSnapshotNextFrame());

    std::vector<uint8_t> nextVideoFrame = {0xFF, 0xD8, 0x11, 0x22, 0x33};
    callBroadcastFrame(nextVideoFrame);

    EXPECT_EQ(getSnapshotBuffer(), nextVideoFrame);
    EXPECT_FALSE(getSnapshotNextFrame());
}

TEST_F(MjpegStreamTest, DebouncesRapidHardwareButtonPresses) {
    callHardwareButtonCallback();
    EXPECT_TRUE(getButtonIsDepressed());

    advanceTime(std::chrono::milliseconds(20));
    callHardwareButtonCallback(); 

    advanceTime(std::chrono::milliseconds(250));

    callCheckForButtonQuickPress();
    EXPECT_TRUE(getSnapshotNextFrame());
    
    setSnapshotNextFrame(false);

    callHardwareButtonCallback();

    for (int i = 0; i < 12; ++i) {
        advanceTime(std::chrono::milliseconds(50));
        callHardwareButtonCallback();
    }

    advanceTime(std::chrono::milliseconds(250)); 
    
    callCheckForButtonQuickPress();
    
    EXPECT_FALSE(getSnapshotNextFrame());
}