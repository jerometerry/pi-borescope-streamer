#include <gtest/gtest.h>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
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

class ApplicationContextComponentsTest : public ::testing::Test {
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

TEST_F(ApplicationContextComponentsTest, PipelineIgnoresEmptyFrames) {
    uint32_t frameId = 0;
    pipeline().updateFrame(nullptr);
    
    auto frame = pipeline().getCurrentFrame(frameId);
    EXPECT_TRUE(frame->empty());
    EXPECT_EQ(frameId, 0);
}

TEST_F(ApplicationContextComponentsTest, HardwareButtonTriggersSnapshotOnNextFrame) {
    buttonManager().registerHardwarePress();
    
    advanceTime(std::chrono::milliseconds(250));

    ASSERT_TRUE(buttonManager().checkAndResetQuickPressTrigger());
    pipeline().requestSnapshot();

    std::vector<uint8_t> nextVideoFrame = {0xFF, 0xD8, 0x11, 0x22, 0x33, 0xFF, 0xD9};
    auto buffer = pipeline().checkoutBuffer();
    ASSERT_NE(buffer, nullptr);
    
    buffer->assign(nextVideoFrame.begin(), nextVideoFrame.end());

    pipeline().updateFrame(std::move(buffer));

    auto snapshotPtr = pipeline().getSnapshot();
    ASSERT_NE(snapshotPtr, nullptr);

    EXPECT_EQ(*snapshotPtr, nextVideoFrame);
}

TEST_F(ApplicationContextComponentsTest, DebouncesRapidHardwareButtonPresses) {
    buttonManager().registerHardwarePress();

    advanceTime(std::chrono::milliseconds(20));
    buttonManager().registerHardwarePress(); 

    advanceTime(std::chrono::milliseconds(250));
    EXPECT_TRUE(buttonManager().checkAndResetQuickPressTrigger());
    
    buttonManager().registerHardwarePress();
    for (int i = 0; i < 12; ++i) {
        advanceTime(std::chrono::milliseconds(50));
        buttonManager().registerHardwarePress();
    }

    advanceTime(std::chrono::milliseconds(250)); 
    EXPECT_FALSE(buttonManager().checkAndResetQuickPressTrigger());
}
