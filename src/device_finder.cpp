#include "device_finder.hpp"
#include "usb_client.hpp"
#include <libusb.h>

DeviceFinder::DeviceFinder() {
}

DeviceFinder::~DeviceFinder() {
}

std::vector<DeviceInfo> DeviceFinder::listDevices(bool onlySuperCameras) {
    std::vector<DeviceInfo> usbDevices;
    libusb_context* ctx = nullptr;
    
    if (UsbClient::initContext(&ctx, nullptr, 0) < 0) {
        return usbDevices;
    }

    libusb_device** devices = nullptr;
    ssize_t count = UsbClient::getDeviceList(ctx, &devices);
    if (count < 0) {
        UsbClient::exit(ctx);
        return usbDevices;
    }

    for (ssize_t i = 0; i < count; ++i) {
        libusb_device* device = devices[i];
        struct libusb_device_descriptor desc{};
        
        if (UsbClient::getDeviceDescriptor(device, &desc) < 0) { 
            continue; 
        }

        bool isSuperCamera = std::ranges::any_of(VENDOR_PRODUCT_ID_LIST,
            [&desc](const auto& vp) {
                return desc.idVendor == vp.first && desc.idProduct == vp.second;
            });

        if (onlySuperCameras && !isSuperCamera) {
            continue;
        }

        DeviceInfo info{
            .bus = UsbClient::getBusNumber(device),
            .address = UsbClient::getDeviceAddress(device),
            .vendorId = desc.idVendor,
            .productId = desc.idProduct,
            .isSuperCamera = isSuperCamera
        };

        libusb_device_handle* handle = nullptr;
        if (UsbClient::open(device, &handle) == 0) {
            unsigned char strBuf[256];
            
            if (desc.iManufacturer && 
                UsbClient::getStringDescriptorAscii(handle, desc.iManufacturer, strBuf, sizeof(strBuf)) > 0) {
                info.manufacturer = reinterpret_cast<char*>(strBuf);
            }
            if (desc.iProduct && 
                UsbClient::getStringDescriptorAscii(handle, desc.iProduct, strBuf, sizeof(strBuf)) > 0) {
                info.product = reinterpret_cast<char*>(strBuf);
            }
            if (desc.iSerialNumber && 
                UsbClient::getStringDescriptorAscii(handle, desc.iSerialNumber, strBuf, sizeof(strBuf)) > 0) {
                info.serialNumber = reinterpret_cast<char*>(strBuf);
            }
                
            UsbClient::close(handle);
        }
        usbDevices.push_back(info);
       
    }

    UsbClient::freeDeviceList(devices, 1);
    UsbClient::exit(ctx);
    return usbDevices;
}
