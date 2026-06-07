#include <algorithm>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <string>
#include <thread>
#include <vector>
#include <memory>
#include "buffer_pool.hpp"
#include "constants.hpp"
#include "shared_frame_buffer.hpp"
#include "mjpeg_data_structures.hpp"

static void pushFrame (SharedFrameBuffer& sfb, const std::shared_ptr<BufferPool>& bp, std::vector<uint8_t>& data) {
    Mjpeg::Frame frame = bp->acquire();
    frame->insertContent(data);
    sfb.push(frame);
};

TEST(BufferPoolTest, BoundedPoolGrowth) {
    auto bufferPool = BufferPool::create();
    SharedFrameBuffer frameBuffer;

    std::vector<Mjpeg::Frame> slowConsumers;
    std::vector<uint8_t> dummyFrame = { 0xDE, 0xAD, 0xBE, 0xEF };

    constexpr int SPIKE_SIZE = 10;

    for (int i = 0; i < SPIKE_SIZE; ++i) {
        pushFrame(frameBuffer, bufferPool, dummyFrame);
        uint32_t id = 0;
        slowConsumers.push_back(frameBuffer.getLatestFrame(id));
    }

    slowConsumers.clear();

    size_t currentPoolSize = bufferPool->getFreeBuffers();
    
    EXPECT_EQ(currentPoolSize, BufferPoolConfig::MAX_POOL_SIZE);
}

TEST(BufferPoolTest, DefaultTotalCapacity128K) {
    auto bufferPool = BufferPool::create();
    auto frame = bufferPool->acquire();
    auto expectedCapacity = Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES + Mjpeg::Buffer::PREFIX_SIZE;
    EXPECT_EQ(frame->totalCapacity(), expectedCapacity);
}

TEST(BufferPoolTest, FrameReserveAddsPrefixSize) {
    BufferPool::BufferPoolArgs args = {
        3, 1, Units::ONE_KILOBYTE
    };
    auto bufferPool = BufferPool::create(args);
    auto frame = bufferPool->acquire();
    auto expectedCapacity = args.bufferReserveSize + Mjpeg::Buffer::PREFIX_SIZE;
    EXPECT_EQ(frame->totalCapacity(), expectedCapacity);
}

TEST(BufferPoolTest, RawBufferPointerManipulation) {
    BufferPool::BufferPoolArgs args = {
        3, 1, Units::ONE_KILOBYTE
    };

    auto bufferPool = BufferPool::create(args);

    auto frame = bufferPool->acquire();
    auto* buffer = frame.getBuffer();

    constexpr std::string_view mjpegHeader = "--mjpegstream\r\nContent-Type: image/jpeg\r\n";

    // Get span<uinit_8> that covers the 128 byte reserved memory inside the buffer
    auto prefix = buffer->getMutablePrefixSlice();

    // Initialize the entire prefix memory with all zeros
    std::ranges::fill(prefix, 0);

    // Convert the span into a raw pointer
    char* ptr = reinterpret_cast<char*>(prefix.data());

    // Keep a separate cursor for remembering our position
    char* cursor = ptr;
    std::memcpy(cursor, mjpegHeader.data(), mjpegHeader.size());
    // move the pointer forward by the number of bytes in the header
    cursor += mjpegHeader.size();

    // Insert 128 bytes of content, initialized to all 0xFF
    buffer->insertContent(std::vector<uint8_t>(128, 0xFF));


    // build a replica of the buffer in separate memory, for comparison

    // Populate the header with the same string
    std::vector<uint8_t> headerData(128, 0);
    cursor = reinterpret_cast<char*>(headerData.data());
    std::memcpy(cursor, mjpegHeader.data(), mjpegHeader.size());
    // just to reiterate that you need to keep track of your position
    cursor += mjpegHeader.size();

    // Initialize content with the same 128 bytes of 0xFF as we did for the Buffer
    std::vector<uint8_t> contentData(128, 0xFF);

    // For the expected data by concatenating the 128 byte header with the 128 byte content
    std::vector<uint8_t> expectedData;
    expectedData.insert(expectedData.end(), headerData.begin(), headerData.end());
    expectedData.insert(expectedData.end(), contentData.begin(), contentData.end());

    // Get the read only span of the Buffer's underlying std::vector, which includes the 128 byte reserved prefix
    auto actualData = frame->all();

    EXPECT_THAT(
        actualData, 
        ::testing::ElementsAreArray(expectedData.begin(), expectedData.end())
    ) << "Frame memory management error";
}
