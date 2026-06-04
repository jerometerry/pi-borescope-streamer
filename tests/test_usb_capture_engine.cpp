#include <gtest/gtest.h>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "usb_capture_engine.hpp"

class UsbCaptureEngineTest : public ::testing::Test {
public:
    std::unique_ptr<UsbCaptureEngine>& getEngine() { return engine; }
    const std::vector<uint8_t>& getInterceptedData() const { return interceptedData; }

private:
    std::unique_ptr<UsbCaptureEngine> engine;
    std::vector<uint8_t> interceptedData;

protected:
    void SetUp() override {
        interceptedData.clear();

        auto testDataSink = [this](std::span<const uint8_t> data) {
            interceptedData.assign(data.begin(), data.end());
        };

        engine = std::make_unique<UsbCaptureEngine>(testDataSink);
    }
};

TEST_F(UsbCaptureEngineTest, HandlesSuccessfulTransfer) {
    std::vector<uint8_t> fakeCameraData = { 0xBB, 0xAA, 0x01, 0x02 };

    bool shouldResubmit = getEngine()->processTransfer(UsbTransferStatus::Completed, fakeCameraData);

    EXPECT_TRUE(shouldResubmit) << "Engine failed to signal hardware to resubmit data.";
    EXPECT_EQ(getInterceptedData(), fakeCameraData) << "Engine failed to route data to sink.";
}

TEST_F(UsbCaptureEngineTest, HandlesDeviceDisconnect) {    
    bool shouldResubmit = getEngine()->processTransfer(UsbTransferStatus::Disconnected, {});

    EXPECT_FALSE(shouldResubmit) << "Engine should block hardware resubmission on disconnect.";
}