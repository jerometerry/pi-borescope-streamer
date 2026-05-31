#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>
#include "clock.hpp"
#include "server_time.hpp"
#include "shared_frame_pipeline.hpp"
#include "hardware_button_manager.hpp"

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

class MjpegStreamComponentsTest : public ::testing::Test {
private:
    TestClock clock_;
    ServerTime serverTime_{clock_, clock_.now()};

    SharedFramePipeline pipeline_;
    std::unique_ptr<HardwareButtonManager> buttonManager_;

protected:
    void SetUp() override {
        buttonManager_ = std::make_unique<HardwareButtonManager>(serverTime_);
    }

    void advanceTime(std::chrono::milliseconds ms) {
        clock_.advance(ms);
    }

    SharedFramePipeline& pipeline() { return pipeline_; }
    HardwareButtonManager& buttonManager() { return *buttonManager_; }
};

// 1. Test Pipeline Frame Operations
TEST_F(MjpegStreamComponentsTest, PipelineIgnoresEmptyFrames) {
    uint32_t frameId = 0;
    pipeline().updateFrame({});
    
    auto frame = pipeline().getCurrentFrame(frameId);
    EXPECT_TRUE(frame.empty());
    EXPECT_EQ(frameId, 0);
}

// 2. Test Pipeline Snapshot Operations
TEST_F(MjpegStreamComponentsTest, HardwareButtonTriggersSnapshotOnNextFrame) {
    buttonManager().registerHardwarePress();
    
    advanceTime(std::chrono::milliseconds(250));

    // Verify the button manager flags a valid quick press window
    ASSERT_TRUE(buttonManager().checkAndResetQuickPressTrigger());
    pipeline().requestSnapshot();

    std::vector<uint8_t> nextVideoFrame = {0xFF, 0xD8, 0x11, 0x22, 0x33, 0xFF, 0xD9};
    pipeline().updateFrame(nextVideoFrame);

    EXPECT_EQ(pipeline().getSnapshot(), nextVideoFrame);
}

// 3. Test Button Manager Debouncing Mechanics
TEST_F(MjpegStreamComponentsTest, DebouncesRapidHardwareButtonPresses) {
    buttonManager().registerHardwarePress();

    // Sudden rapid chattering edge signals (ignored by debounce window)
    advanceTime(std::chrono::milliseconds(20));
    buttonManager().registerHardwarePress(); 

    advanceTime(std::chrono::milliseconds(250));
    EXPECT_TRUE(buttonManager().checkAndResetQuickPressTrigger());
    
    // Test a long hold window that triggers the bypass logic
    buttonManager().registerHardwarePress();
    for (int i = 0; i < 12; ++i) {
        advanceTime(std::chrono::milliseconds(50));
        buttonManager().registerHardwarePress();
    }

    advanceTime(std::chrono::milliseconds(250)); 
    EXPECT_FALSE(buttonManager().checkAndResetQuickPressTrigger());
}
