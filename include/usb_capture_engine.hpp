#pragma once
#include <atomic>
#include <memory>
#include <thread>
class HardwareButtonManager;
class SharedFramePipeline;
class UsbCamera;
class UsbFrameDecoder;
struct DeviceInfo;

/** 
 * @brief Class representing the USB capture engine
 */
class UsbCaptureEngine {
public:
    /** 
     * @brief Construct a new USB capture engine instance
     * @param pipeline The shared frame pipeline
     * @param buttonManager The hardware button manager
     * @param running The running flag
     */
    UsbCaptureEngine(
        SharedFramePipeline& pipeline, 
        HardwareButtonManager& buttonManager, 
        std::atomic<bool>& running
    );

    /** 
     * @brief Destroy the USB capture engine instance
     */
    ~UsbCaptureEngine();

    /** 
     * @brief Start the USB capture engine
     * @param target The target USB device
     */
    void start(const DeviceInfo& target);

    /** 
     * @brief Stop the USB capture engine
     */
    void stop();

private:
    /** 
     * @brief The USB camera instance
     */
    std::unique_ptr<UsbCamera> camera_;

    /** 
     * @brief The USB frame decoder instance
     */
    std::unique_ptr<UsbFrameDecoder> decoder_;

    /** 
     * @brief The shared frame pipeline
     */
    SharedFramePipeline& pipeline_;

    /** 
     * @brief The hardware button manager
     */
    HardwareButtonManager& buttonManager_;

    /** 
     * @brief The running flag
     */
    std::atomic<bool>& running_;

    /** 
     * @brief The worker thread
     */
    std::thread workerThread_;

    /** 
     * @brief The main loop for the capture engine
     * @param target The target USB device
     */
    void loop(const DeviceInfo& target);
};
