#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "usb_device_finder.hpp"
#include "usb_device_info.hpp"

class DeviceFinderTest : public ::testing::Test {
   private:
    std::vector<UsbDeviceInfo> mockCameras_{};

   protected:
    void SetUp() override {}

    void addMockCamera(const UsbDeviceInfo& device) {
        mockCameras_.push_back(device);
    }

    std::string devicesToJson() const {
        return UsbDeviceFinder::toJson(mockCameras_);
    }
};

TEST_F(DeviceFinderTest, SingleDeviceToJson) {
    UsbDeviceInfo info{1, 2, 4660, 22136, "TestCam Inc.", "TestCam Model X", "TEST123456", true};

    addMockCamera(info);
    auto json = devicesToJson();

    EXPECT_EQ(json,
              "[{\"bus\":1,\"address\":2,\"vendorId\":4660,\"productId\":22136,\"manufacturer\":"
              "\"TestCam Inc.\",\"product\":\"TestCam Model "
              "X\",\"serialNumber\":\"TEST123456\",\"isSuperCamera\":true}]");
}

TEST_F(DeviceFinderTest, MultipleDevicesToJson) {
    UsbDeviceInfo info1{1, 2, 4660, 22136, "TestCam Inc.", "TestCam Model X", "TEST123456", true};
    UsbDeviceInfo info2{3, 4, 4660, 22136, "TestCam Inc.", "TestCam Model X", "TEST789012", true};

    addMockCamera(info1);
    addMockCamera(info2);
    auto json = devicesToJson();

    EXPECT_EQ(json,
              "[{\"bus\":1,\"address\":2,\"vendorId\":4660,\"productId\":22136,\"manufacturer\":"
              "\"TestCam Inc.\",\"product\":\"TestCam Model "
              "X\",\"serialNumber\":\"TEST123456\",\"isSuperCamera\":true},{\"bus\":3,\"address\":"
              "4,\"vendorId\":4660,\"productId\":22136,\"manufacturer\":\"TestCam "
              "Inc.\",\"product\":\"TestCam Model "
              "X\",\"serialNumber\":\"TEST789012\",\"isSuperCamera\":true}]");
}
