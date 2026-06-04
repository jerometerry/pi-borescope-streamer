#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "usb_capture_engine.hpp"
#include "shared_frame_pipeline.hpp"
#include "mjpeg_frame_decoder.hpp"
#include "device_info.hpp"

class MockSharedFramePipeline : public SharedFramePipeline {
public:
    MOCK_METHOD(std::shared_ptr<std::vector<uint8_t>>, checkoutBuffer, (), (override));
    MOCK_METHOD(void, updateFrame, (std::shared_ptr<std::vector<uint8_t>>), (override));
};

class MockMjpegFrameDecoder : public MjpegFrameDecoder {
public:
	 explicit MockMjpegFrameDecoder(std::function<void(const std::vector<uint8_t>&)> handler)
        : MjpegFrameDecoder(std::move(handler)) {}
    MOCK_METHOD(void, processIncomingCameraData, (std::span<const uint8_t>), (override));
};

class UsbCaptureEngineTest : public ::testing::Test {
public:
	MockSharedFramePipeline& getMockPipeline() { return mockPipeline; }
	std::unique_ptr<UsbCaptureEngine>& getEngine() { return engine; }
	std::atomic<bool>& getIsRunning() { return isRunning; }

private:
    MockSharedFramePipeline mockPipeline;
    std::atomic<bool> isRunning{true};
    std::unique_ptr<UsbCaptureEngine> engine;

protected:
    void SetUp() override {
        isRunning = true;
        engine = std::make_unique<UsbCaptureEngine>(mockPipeline, isRunning);
    }

    void TearDown() override {
        engine->stop();
    }
};

TEST_F(UsbCaptureEngineTest, StartsAndStopsCleanlyWithoutHardware) {
    DeviceInfo fakeTarget{ .bus = 1, .address = 1, .vendorId = 0x0000, .productId = 0x0000 };

    getEngine()->start(fakeTarget);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_FALSE(getIsRunning().load()) << "Engine should flip atomic flag to false if hardware is missing.";
}

TEST_F(UsbCaptureEngineTest, HandlesSuccessfulTransfer) {
	std::function<void(const std::vector<uint8_t>&)> broadcastHandler = 
		[](const std::vector<uint8_t>&) {};

	auto mockDecoder = std::make_unique<MockMjpegFrameDecoder>(broadcastHandler);

    EXPECT_CALL(*mockDecoder, processIncomingCameraData(testing::_))
        .WillOnce([](std::span<const uint8_t> payload) {
            EXPECT_EQ(payload.size(), 4);
            EXPECT_EQ(payload[0], 0xBB);
            EXPECT_EQ(payload[1], 0xAA);
        });

    getEngine()->decoder_ = std::move(mockDecoder);

    std::vector<uint8_t> fakeCameraData = { 0xBB, 0xAA, 0x01, 0x02 };
    
    libusb_transfer fakeTransfer{};
    fakeTransfer.status = LIBUSB_TRANSFER_COMPLETED;
    fakeTransfer.buffer = fakeCameraData.data();
    fakeTransfer.actual_length = static_cast<int>(fakeCameraData.size());

    getIsRunning() = false; 
    
    getEngine()->handleIncomingTransfer(&fakeTransfer);
}

TEST_F(UsbCaptureEngineTest, HandlesDeviceDisconnect) {    
    libusb_transfer fakeTransfer{};
    fakeTransfer.status = LIBUSB_TRANSFER_NO_DEVICE;

    getEngine()->handleIncomingTransfer(&fakeTransfer);

    EXPECT_FALSE(getIsRunning().load()) << "Router failed to kill the engine on USB disconnect.";
}