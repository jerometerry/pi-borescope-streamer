#pragma once
#include <cstdint>
#include <span>

#include "video_frame_buffer.hpp"

class V4l2MjpegStream {
   public:
    /**
     * @brief Construct the decoder and wire up its output destinations.
     */
    explicit V4l2MjpegStream(VideoFrameBuffer& disruptor);

    ~V4l2MjpegStream() = default;

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
    VideoFrameBuffer* disruptor_;

    /**
     * @brief
     */
    int64_t currentClaimSequence_{-1};
};
