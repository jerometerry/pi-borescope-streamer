#include <gtest/gtest.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "usb_capture_engine.hpp"
#include "shared_frame_pipeline.hpp"
#include "mjpeg_frame_decoder.hpp"
#include "device_info.hpp"

class UsbCaptureEngineTest : public ::testing::Test {
public:
    SharedFramePipeline& getPipeline() { return realPipeline; }
    std::unique_ptr<UsbCaptureEngine>& getEngine() { return engine; }
    std::atomic<bool>& getIsRunning() { return isRunning; }

private:
    SharedFramePipeline realPipeline;
    std::atomic<bool> isRunning{true};
    std::unique_ptr<UsbCaptureEngine> engine;

protected:
    void SetUp() override {
        isRunning = true;
        engine = std::make_unique<UsbCaptureEngine>(realPipeline, isRunning);
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
    getEngine()->decoder_ = std::make_unique<MjpegFrameDecoder>(
        [](const std::vector<uint8_t>&) {}
    );

    std::vector<uint8_t> fakeCameraData = { 0xBB, 0xAA, 0x01, 0x02 };
    
    libusb_transfer fakeTransfer{};
    fakeTransfer.status = LIBUSB_TRANSFER_COMPLETED;
    fakeTransfer.buffer = fakeCameraData.data();
    fakeTransfer.actual_length = static_cast<int>(fakeCameraData.size());

    // 3. Set running to false so the engine doesn't try to submit the fake transfer back to the OS
    getIsRunning() = false; 
    
    // 4. Fire the transfer. If the engine correctly spans the memory and the decoder digests it
    //    without a segfault or underflow, the data pipeline is rock solid.
    getEngine()->handleIncomingTransfer(&fakeTransfer);
    
    SUCCEED();
}

TEST_F(UsbCaptureEngineTest, HandlesDeviceDisconnect) {    
    libusb_transfer fakeTransfer{};
    fakeTransfer.status = LIBUSB_TRANSFER_NO_DEVICE;

    getEngine()->handleIncomingTransfer(&fakeTransfer);

    EXPECT_FALSE(getIsRunning().load()) << "Router failed to kill the engine on USB disconnect.";
}