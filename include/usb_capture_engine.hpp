#pragma once
#include <gtest/gtest_prod.h>
#include <libusb.h>
#include <stdint.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <thread>
#include <vector>

class MjpegFrameDecoder;
class SharedFramePipeline;
class UsbCamera;
struct DeviceInfo;

/**
 * @brief The dedicated background worker that continuously pumps data from the physical camera.
 * @details If the rest of the software is a video processing factory, this class is the intake pump. 
 * It claims the physical USB port, spins up its own dedicated background thread, and aggressively 
 * reads raw data off the wire as fast as the camera can send it.
 * 
 * It also acts as the foreman for the intake process: as raw data pours in, it hands the bytes 
 * to the MjpegFrameDecoder. When the decoder successfully extracts a clean picture, this engine immediately routes
 * those finished products into the SharedFramePipeline.
 */
class UsbCaptureEngine {
public:
    /**
     * @brief Prepare the intake pump and wire its outputs to the rest of the system.
     * @details This sets up the routing connections but does not actually turn the pump on 
     * or claim the USB port yet.
     * @param pipeline The memory exchange zone where finished pictures will be dropped.
     * @param running The master emergency stop switch that keeps the background thread alive.
     */
    UsbCaptureEngine(
        std::function<void(std::span<const uint8_t>)> dataSink,
        std::atomic<bool>& running
    );

    /**
     * @brief Safely destroy the engine and release the hardware.
     */
    ~UsbCaptureEngine();

    /**
     * @brief Power on the pump and start pulling data from the hardware.
     * @details Connects to the physical endoscope, spins up a dedicated background thread, 
     * and begins the infinite loop of reading USB data.
     * @param target The specific hardware ID of the camera to connect to.
     */
    void start(const DeviceInfo& target);

    /**
     * @brief Safely shut down the background pumping thread.
     * @details Waits for the worker thread to finish its current USB read and cleanly exit.
     */
    void stop();

private:
    FRIEND_TEST(UsbCaptureEngineTest, HandlesSuccessfulTransfer);
    FRIEND_TEST(UsbCaptureEngineTest, HandlesDeviceDisconnect);

    /**
     * @brief The direct, low-level connection to the physical endoscope hardware.
     */
    std::unique_ptr<UsbCamera> camera_;

    /**
     * @brief
     */
    std::function<void(std::span<const uint8_t>)> dataSink_;

    /**
     * @brief The global kill switch that keeps the infinite pumping loop running.
     */
    std::atomic<bool>& running_;

    /**
     * @brief The dedicated background thread doing all the heavy lifting.
     * @details Isolating the USB reads to this specific thread ensures the network server 
     * never freezes or stutters while waiting for the camera hardware to respond.
     */
    std::thread workerThread_;

    std::vector<libusb_transfer*> transferPool_;

    std::vector<std::vector<uint8_t>> transferBuffers_;

    /**
     * @brief The infinite loop that aggressively reads the USB cable.
     * @details This is the heartbeat of the engine. It continuously scoops 4-Kilobyte buckets 
     * of raw data from the USB cable and dumps them directly into the decoder. If the USB 
     * cable is physically unplugged (LIBUSB_ERROR_NO_DEVICE), it safely flips the global 
     * kill switch to shut the whole server down.
     * @param target The hardware device we are looping against.
     */
    void loop(const DeviceInfo& target);

    static void LIBUSB_CALL transferCallback(struct libusb_transfer* transfer);

    void handleIncomingTransfer(struct libusb_transfer* transfer);  
};
