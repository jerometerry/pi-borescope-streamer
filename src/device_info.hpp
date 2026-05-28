#pragma once

#include <cstdint>
#include <string>

struct DeviceInfo {
    uint8_t bus;
    uint8_t address;
    uint16_t vendorId;
    uint16_t productId;
    std::string manufacturer;
    std::string product;
    std::string serialNumber;
    bool isSuperCamera;
};