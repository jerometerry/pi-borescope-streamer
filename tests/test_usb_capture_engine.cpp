#include <gtest/gtest.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "usb_capture_engine.hpp"
#include "device_info.hpp"

class UsbCaptureEngineTest : public ::testing::Test {
public:
    std::unique_ptr<UsbCaptureEngine>& getEngine() { return engine; }
    std::atomic<bool>& getIsRunning() { return isRunning; }
    const std::vector<uint8_t>& getInterceptedData() const { return interceptedData; }

private:
    std::atomic<bool> isRunning{true};
    std::unique_ptr<UsbCaptureEngine> engine;
    std::vector<uint8_t> interceptedData;

protected:
    void SetUp() override {
        isRunning = true;
        interceptedData.clear();

        auto testDataSink = [this](std::span<const uint8_t> data) {
            interceptedData.assign(data.begin(), data.end());
        };
        
        engine = std::make_unique<UsbCaptureEngine>(testDataSink, isRunning);
    }

    void TearDown() override {
        if (engine) {
            engine->stop();
        }
    }
};

TEST_F(UsbCaptureEngineTest, StartsAndStopsCleanlyWithoutHardware) {
    DeviceInfo fakeTarget{ .bus = 1, .address = 1, .vendorId = 0x0000, .productId = 0x0000 };

    getEngine()->start(fakeTarget);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_FALSE(getIsRunning().load()) << "Engine should flip atomic flag to false if hardware is missing.";
}

TEST_F(UsbCaptureEngineTest, HandlesSuccessfulTransfer) {
    std::vector<uint8_t> fakeCameraData = { 0xBB, 0xAA, 0x01, 0x02 };
    
    libusb_transfer fakeTransfer{};
    fakeTransfer.status = LIBUSB_TRANSFER_COMPLETED;
    fakeTransfer.buffer = fakeCameraData.data();
    fakeTransfer.actual_length = static_cast<int>(fakeCameraData.size());

    getIsRunning() = false; 

    getEngine()->handleIncomingTransfer(&fakeTransfer);

    EXPECT_EQ(getInterceptedData(), fakeCameraData) 
        << "The engine failed to correctly route the USB payload to the data sink.";
}

TEST_F(UsbCaptureEngineTest, HandlesDeviceDisconnect) {    
    libusb_transfer fakeTransfer{};
    fakeTransfer.status = LIBUSB_TRANSFER_NO_DEVICE;

    getEngine()->handleIncomingTransfer(&fakeTransfer);

    EXPECT_FALSE(getIsRunning().load()) << "Router failed to kill the engine on USB disconnect.";
}