#include "device_finder.hpp"
#include "usb_context.hpp"
#include "usb_device_list.hpp"
#include <libusb.h>

std::vector<DeviceInfo> DeviceFinder::list(bool onlySuperCameras) {
    std::vector<DeviceInfo> usbDevices;
    UsbContext context;
    UsbDeviceList list(context);

    for (libusb_device* device : list.get()) {
        struct libusb_device_descriptor desc{};
        
        if (libusb_get_device_descriptor(device, &desc) < 0) { 
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
            .bus = libusb_get_bus_number(device),
            .address = libusb_get_device_address(device),
            .vendorId = desc.idVendor,
            .productId = desc.idProduct,
            .isSuperCamera = isSuperCamera
        };

        libusb_device_handle* handle = nullptr;
        if (libusb_open(device, &handle) == 0) {
            unsigned char strBuf[256];
            
            if (desc.iManufacturer && 
                libusb_get_string_descriptor_ascii(handle, desc.iManufacturer, strBuf, sizeof(strBuf)) > 0) {
                info.manufacturer = reinterpret_cast<char*>(strBuf);
            }
            if (desc.iProduct && 
                libusb_get_string_descriptor_ascii(handle, desc.iProduct, strBuf, sizeof(strBuf)) > 0) {
                info.product = reinterpret_cast<char*>(strBuf);
            }
            if (desc.iSerialNumber && 
                libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, strBuf, sizeof(strBuf)) > 0) {
                info.serialNumber = reinterpret_cast<char*>(strBuf);
            }
                
            libusb_close(handle);
        }
        usbDevices.push_back(info);
       
    }

    return usbDevices;
}

libusb_device_handle* DeviceFinder::open(UsbContext& context, const DeviceInfo& target) {
    UsbDeviceList list(context);
    
    for (libusb_device* device : list.get()) {
        if (libusb_get_bus_number(device) == target.bus && 
            libusb_get_device_address(device) == target.address) {
            
            libusb_device_handle* handle = nullptr;
            if (libusb_open(device, &handle) == 0) {
                return handle;
            }
        }
    }
    return nullptr;
}