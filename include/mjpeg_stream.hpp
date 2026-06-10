#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>
#include "buffer_ptr.hpp"
#include "constants.hpp"
#include "disruptor.hpp"
#include "hardcore_video_frame.hpp"
#include "usb_payload_header.hpp"

/**
 * @brief The translator that extracts and reassembles clean MJPEG video pictures from the hardware stream.
 * @details The physical camera does not send nice, neat video files. It chops the MJPEG video 
 * up into hundreds of tiny chunks, slaps a custom "shipping label" on each one, and fires 
 * them down the wire. To make matters worse, buggy camera hardware sometimes inserts 
 * broken or "ghost" labels into the data stream.
 *
 * MjpegStream acts as the sorting facility. It takes the raw firehose of data from 
 * the transport layer, throws out the glitches, and carefully stitches the valid chunks back 
 * together into standard JPEG images. Once it successfully builds a complete picture, 
 * it hands it off to be broadcast. It also actively scans the hidden status signals in 
 * the chunks and alerts the system whenever the user's finger is holding down the physical hardware button.
 */
class MjpegStream {
public:
    /**
     * @brief Construct the decoder and wire up its output destinations.
     * @param bufferPool description
     * @param onFrameReady The function we call to hand off a finished, clean JPEG picture. 
     * Usually, this connects to the MjpegServer so the picture can be sent to web browsers.
     */
    explicit MjpegStream(FrameDisruptor& disruptor);

    ~MjpegStream() = default;

    /**
     * @brief Pour new raw data from the camera cable into the decoder.
     * @details This is where the raw data enters the sorting facility. The decoder will 
     * read the labels, stitch the data into the current picture, and trigger the handlers 
     * if a picture is completed or a button press is detected.
     * @param data A raw slice of bytes directly from the hardware.
     */
    void send(std::span<const uint8_t> data);

private:
    /**
     * @brief 
     */
    FrameDisruptor* disruptor_;

    /**
     * @brief 
     */
    int64_t currentClaimSequence_{-1};

    /**
     * @brief 
     */
    bool frameActive_{false};

    /**
     * @brief The waiting room for raw bytes that haven't been sorted yet.
     */
    std::vector<uint8_t> inputBuffer_{};

    /**
     * @brief A bookmark tracking how far we've read into the stream buffer.
     * @details Prevents us from having to constantly shift memory around or re-read 
     * data we've already processed, keeping the decoder lightning fast.
     */
    size_t readOffset_{0};

    /**
     * @brief The memory of what the current picture is supposed to look like.
     * @details Keeps track of things like the current frame ID and which lens the 
     * data is coming from, so we don't accidentally stitch chunks from two different 
     * pictures together.
     */
    UsbPayloadHeader payloadHeader_{};

    /**
     * @brief Get the Active Frame Slot object
     * 
     * @return HardcoreVideoFrame& 
     */
    HardcoreVideoFrame& getActiveFrameSlot();

    /**
     * @brief Snip out the exact picture and send it off.
     * @details Standard JPEG files have strict start (`FF D8`) and end (`FF D9`) markers. 
     * This function scans the workbench, cuts out the JPEG file, and fires it 
     * into the `frameSink`.
     */
    void outputFrame();
};
