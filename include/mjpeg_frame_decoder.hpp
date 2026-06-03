#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <span>
#include <vector>
#include "data_structures.hpp"

/**
 * @brief The translator that extracts and reassembles clean MJPEG video pictures from the hardware stream.
 * @details The physical camera does not send nice, neat video files. It chops the MJPEG video 
 * up into hundreds of tiny chunks, slaps a custom "shipping label" on each one, and fires 
 * them down the wire. To make matters worse, buggy camera hardware sometimes inserts 
 * broken or "ghost" labels into the data stream.
 * 
 * MjpegFrameDecoder acts as the sorting facility. It takes the raw firehose of data from 
 * the transport layer, throws out the glitches, and carefully stitches the valid chunks back 
 * together into standard JPEG images. Once it successfully builds a complete picture, 
 * it hands it off to be broadcast. It also actively scans the hidden status signals in 
 * the chunks and alerts the system whenever the user's finger is holding down the physical hardware button.
 */
class MjpegFrameDecoder {
public:
    /**
     * @brief Construct the decoder and wire up its output destinations.
     * @param broadcastHandler The function we call to hand off a finished, clean JPEG picture. 
     * Usually, this connects to the MjpegServer so the picture can be sent to web browsers.
     */
    explicit MjpegFrameDecoder(std::function<void(const std::vector<uint8_t>&)> broadcastHandler);

    /**
     * @brief Pour new raw data from the camera cable into the decoder.
     * @details This is where the raw data enters the sorting facility. The decoder will 
     * read the labels, stitch the data into the current picture, and trigger the handlers 
     * if a picture is completed or a button press is detected.
     * @param data A raw slice of bytes directly from the hardware.
     */
    void processIncomingCameraData(std::span<const uint8_t> data);

private:

    /**
     * @brief The exact byte size of the camera's custom shipping label.
     */
    static constexpr size_t USB_PACKET_HEADER_LENGTH = sizeof(UsbPacketHeader);

    /**
     * @brief The exact byte size of the camera's hidden status report.
     */
    static constexpr size_t CHUNK_METADATA_LENGTH = sizeof(CameraPacketHeader);

    /**
     * @brief The waiting room for raw bytes that haven't been sorted yet.
     */
    std::vector<uint8_t> streamBuffer;

    /**
     * @brief The workbench where we are currently stitching the chunks into a picture.
     */
    std::vector<uint8_t> frameBuffer;

    /**
     * @brief The staging area for a finished picture right before it gets broadcasted.
     */
    std::vector<uint8_t> emitBuffer;

    /**
     * @brief The memory of what the current picture is supposed to look like.
     * @details Keeps track of things like the current frame ID and which lens the 
     * data is coming from, so we don't accidentally stitch chunks from two different 
     * pictures together.
     */
    CameraPacketHeader metadata_{};

    /**
     * @brief A bookmark tracking how far we've read into the stream buffer.
     * @details Prevents us from having to constantly shift memory around or re-read 
     * data we've already processed, keeping the decoder lightning fast.
     */
    size_t readOffset{0};
    
    /**
     * @brief Where to send finished video pictures.
     */
    std::function<void(const std::vector<uint8_t>&)> broadcastHandler;

    /**
     * @brief Snip out the exact picture and send it off.
     * @details Standard JPEG files have strict start (`FF D8`) and end (`FF D9`) markers. 
     * This function scans the workbench, cuts out the perfect JPEG file, and fires it 
     * into the `broadcastHandler`.
     */
    void trimAndEmitFrame();

    /**
     * @brief Check if the chunk actually contains video data.
     * @details The camera sometimes sends empty "heartbeat" chunks. This tells us if 
     * we should bother trying to stitch this chunk into the picture.
     * @param metadata The hidden status report for the chunk.
     * @return True if the chunk contains real picture data.
     */
    static bool fromVideoFeed(CameraPacketHeader metadata);

     /**
     * @brief Safety check to prevent mixing pictures from different camera lenses.
     * @param first The status report of the picture currently on the workbench.
     * @param second The status report of the new chunk we just received.
     * @return True if both belong to the exact same physical lens.
     */
    static bool forSameCamera(CameraPacketHeader first, CameraPacketHeader second);

    /**
     * @brief Safety check to prevent mixing chunks from two different moments in time.
     * @param first The status report of the picture currently on the workbench.
     * @param second The status report of the new chunk we just received.
     * @return True if both belong to the exact same frame ID and lens.
     */
    static bool forSameCameraAndFrame(CameraPacketHeader first, CameraPacketHeader second);
};