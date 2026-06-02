#pragma once

#include <atomic>
#include <functional>
#include "usb_capture_engine.hpp"

class SharedFramePipeline;
class HardwareButtonManager;
class MjpegServer;
struct DeviceInfo;

/**
 * @brief The master switchboard that wires all the individual system components together.
 * @details If you were building this project on a physical workbench, you would have a 
 * camera, a hardware button, a memory chip, and a Wi-Fi antenna. This class acts as the 
 * motherboard or breadboard for the software. 
 * 
 * It takes all the independent, highly-specialized modules (the USB engine, the memory pipeline, 
 * the button filter, and the network server), connects their communication wires together, and 
 * provides a single master power switch to safely start and shut down the entire operation.
 */
class ApplicationContext {
public:
    /**
     * @brief Mount the components onto the motherboard and wire them together.
     * @details This does not turn the system on; it simply establishes the connections 
     * between the camera, the memory pipeline, and the network broadcaster so data can 
     * flow once the power is flipped.
     * @param pipeline The shared memory zone where the camera will drop finished pictures.
     * @param buttonManager The smart filter that will interpret physical button clicks.
     * @param server The network engine that will broadcast the pictures over Wi-Fi.
     * @param running The master emergency stop switch that keeps the background threads alive.
     */
    ApplicationContext(SharedFramePipeline& pipeline, 
                       HardwareButtonManager& buttonManager, 
                       MjpegServer& server,
                       std::atomic<bool>& running);

    /**
     * @brief Safely scrap the motherboard and un-wire the components.
     */
    ~ApplicationContext() = default;

    /**
     * @brief Prevent copying. You cannot magically duplicate a physical motherboard.
     */
    ApplicationContext(const ApplicationContext&) = delete;

    /**
     * @brief Prevent assignment. Enforces that there is only one central system controller.
     * @return A reference to the assigned instance.
     */
    ApplicationContext& operator=(const ApplicationContext&) = delete;

    /**
     * @brief Flip the master power switch and turn the system on.
     * @details Fires up the USB capture engine to start pulling data from the hardware, 
     * launches the network server to start listening for web browsers, and then puts the 
     * main program to sleep while the background threads do all the heavy lifting.
     * @param target The specific hardware ID of the camera we want to connect to.
     * @return 0 (EXIT_SUCCESS) when the system eventually shuts down cleanly.
     */
    int run(const DeviceInfo& target);

    /**
     * @brief Hit the emergency stop switch.
     * @details Flips the global `running` flag to false. This signals the network thread 
     * and the USB thread to finish exactly what they are doing right now, pack up their 
     * memory safely, and shut down.
     */
    void stop();

private:
    /**
     * @brief A borrowed reference to the memory pipeline.
     */
    std::reference_wrapper<SharedFramePipeline> pipeline_;

    /**
     * @brief A borrowed reference to the hardware button filter.
     */
    std::reference_wrapper<HardwareButtonManager> buttonManager_;

    /**
     * @brief A borrowed reference to the network broadcasting engine.
     */
    std::reference_wrapper<MjpegServer> server_;

    /**
     * @brief A borrowed reference to the global emergency stop switch.
     */
    std::reference_wrapper<std::atomic<bool>> running_;

    /**
     * @brief The engine responsible for hijacking the USB port and decoding the video.
     * @details Unlike the other components which are borrowed (`reference_wrapper`), 
     * this engine is physically owned and managed by the motherboard.
     */
    UsbCaptureEngine captureEngine_;
};