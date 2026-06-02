#pragma once

#include <atomic>
#include <functional>
#include "usb_capture_engine.hpp"

class SharedFramePipeline;
class HardwareButtonManager;
class MjpegServer;
struct DeviceInfo;

/** @brief A class representing the application context.
 * This class manages the main components of the application, including the frame pipeline, hardware button manager,
 * web server, and the USB capture engine. It provides methods to run and stop the application.
 */
class ApplicationContext {
public:
    /** @brief Constructs an ApplicationContext instance.
     *  @param pipeline The shared frame pipeline.
     *  @param buttonManager The hardware button manager.
     *  @param server The web server.
     *  @param running The running flag.
     */
    ApplicationContext(SharedFramePipeline& pipeline, 
                HardwareButtonManager& buttonManager, 
                MjpegServer& server,
                std::atomic<bool>& running);

    /** @brief Destructs an ApplicationContext instance.
     */
    ~ApplicationContext() = default;

    /** @brief Copy constructor.
     */
    ApplicationContext(const ApplicationContext&) = delete;

    /** @brief Assignment operator.
     *  @return A reference to the assigned instance.
     */
    ApplicationContext& operator=(const ApplicationContext&) = delete;

    /** @brief Runs the application with the specified target device.
     *  @param target The device information for the target device.
     *  @return The result of the operation.
     */
    int run(const DeviceInfo& target);

    /** @brief Stops the application.
     */
    void stop();

private:
    /** @brief The shared frame pipeline.
     */
    std::reference_wrapper<SharedFramePipeline> pipeline_;

    /** @brief The hardware button manager.
     */
    std::reference_wrapper<HardwareButtonManager> buttonManager_;

    /** @brief The web server.
     */
    std::reference_wrapper<MjpegServer> server_;

    /** @brief The running flag.
     */
    std::reference_wrapper<std::atomic<bool>> running_;

    /** @brief The USB capture engine.
     */
    UsbCaptureEngine captureEngine_;
};
