#pragma once
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <vector>

/**
 * @brief The zero-copy memory bridge between the camera hardware and the network server.
 * @details The camera hardware produces video frames incredibly fast, and the network server 
 * needs to send those frames to multiple viewers. If the camera tried to write a new picture 
 * to memory at the exact same millisecond the network was trying to read it, the picture 
 * would tear or glitch. Furthermore, constantly asking the Raspberry Pi for new memory for 
 * every single video frame would quickly fragment the RAM and crash the system.
 * 
 * SharedFramePipeline solves this by acting as a safe exchange zone. At startup, it creates 
 * a small, fixed number of blank canvases (memory buffers). The camera thread checks out a 
 * blank canvas, fills it with a JPEG, and places it in the active display window. The network 
 * thread can safely read from this display window at any time. When a canvas is no longer 
 * needed, it is returned to the blank pile to be reused. This completely eliminates memory 
 * leaks and keeps the video stream perfectly smooth.
 */
class SharedFramePipeline {
public:
    /**
     * @brief Create the memory exchange zone and pre-allocate the blank canvases.
     */
    SharedFramePipeline();

    /**
     * @brief Replace the picture currently in the display window with a fresh one.
     * @param newFrame The finished video picture straight from the hardware decoder.
     * @details The old picture is instantly recycled back into the blank canvas pool 
     * for the camera to reuse later. If the user recently clicked the hardware snapshot 
     * button, a copy of this frame is safely tucked away before the display window updates.
     */
    void updateFrame(std::shared_ptr<std::vector<uint8_t>> newFrame);

    /**
     * @brief Grab an empty canvas from the recycling pile.
     * @return A safe block of memory ready to be filled with video data.
     * @details The decoder uses this to get a place to start building the next incoming 
     * video picture. Because it just recycles existing memory, this operation is 
     * virtually instantaneous.
     */
    std::shared_ptr<std::vector<uint8_t>> checkoutBuffer();

    /**
     * @brief Throw a canvas back into the recycling pile without showing it to anyone.
     * @param buffer The memory block to recycle.
     * @details Used if the decoder started building a picture, but realized the camera 
     * data was corrupted and decided to abort and throw the picture away.
     */
    void returnBuffer(std::shared_ptr<std::vector<uint8_t>> buffer);

    /**
     * @brief Safely look at the picture currently in the active display window.
     * @param outFrameId A counter that updates with the ID of the returned picture, 
     * so the server knows if it has already broadcasted this exact frame.
     * @return The raw JPEG bytes ready to be sent over the Wi-Fi.
     */
    std::shared_ptr<const std::vector<uint8_t>> getCurrentFrame(uint32_t& outFrameId) const;
    
private:
    /**
     * @brief A lock ensuring two threads don't try to grab the same blank canvas at once.
     */
    mutable std::mutex poolMutex_;

    /**
     * @brief A lock ensuring the camera doesn't swap the active picture while the network is reading it.
     */
    mutable std::mutex activeMutex_;

    /**
     * @brief The recycling pile of empty canvases waiting to be used.
     */
    std::vector<std::shared_ptr<std::vector<uint8_t>>> freePool_;

    /**
     * @brief The active display window showing the absolute newest video picture.
     */
    std::shared_ptr<const std::vector<uint8_t>> latestFrame_;

    /**
     * @brief A rolling counter tracking how many pictures have passed through the pipeline.
     */
    uint32_t frameId_{0};
};