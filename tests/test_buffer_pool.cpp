#include <algorithm>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <format>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <memory>
#include <utility>
#include <vector>
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

// -------------------------------------------------------------------
// FRAME REFERENCE COUNTING & LIFECYCLE TESTS
// Demonstrates safe zero-copy broadcasting across multiple simulated clients
// -------------------------------------------------------------------

TEST(BufferPoolTest, FrameReferenceCountingForMulticast) {
    auto bufferPool = BufferPool::create();
    size_t initialFree = bufferPool->getFreeBuffers();

    {
        Mjpeg::Frame masterFrame = bufferPool->acquire();
        EXPECT_EQ(bufferPool->getFreeBuffers(), initialFree - 1) << "Buffer not removed from pool";

        {
            // Simulate Viewer 1 connecting and holding a copy of the frame
            Mjpeg::Frame viewer1Frame = masterFrame; 
            
            // Simulate Viewer 2 connecting and holding a copy
            Mjpeg::Frame viewer2Frame = masterFrame; 
            
            // Master frame is overwritten with a new frame from the camera
            masterFrame = bufferPool->acquire();
            EXPECT_EQ(bufferPool->getFreeBuffers(), initialFree - 2) << "Second buffer not acquired";

            // At this point, the original buffer is solely kept alive by viewer1 and viewer2
        } // viewer1Frame and viewer2Frame go out of scope here. 
          // The atomic ref count hits 0, and the callback fires.

        // Verify the original buffer was returned to the pool automatically
        EXPECT_EQ(bufferPool->getFreeBuffers(), initialFree - 1) << "Buffer not automatically recycled after all references dropped";
    } // masterFrame goes out of scope here

    // All frames are dead. Pool should be fully restored.
    EXPECT_EQ(bufferPool->getFreeBuffers(), initialFree) << "Pool leak detected";
}

TEST(BufferPoolTest, FrameMoveSemantics) {
    auto bufferPool = BufferPool::create();
    size_t initialFree = bufferPool->getFreeBuffers();

    Mjpeg::Frame original = bufferPool->acquire();
    Mjpeg::Buffer* underlyingPtr = original.getBuffer();

    // Move the frame. This should transfer ownership without touching the atomic counter
    Mjpeg::Frame movedFrame = std::move(original);

    // The original frame should now be completely empty (operator bool() == false)

    bool is_valid = static_cast<bool>(original); // NOLINT(bugprone-use-after-move)
    EXPECT_FALSE(is_valid) << "Moved-from frame still holds a valid state";

    // The new frame should point to the exact same memory
    EXPECT_EQ(movedFrame.getBuffer(), underlyingPtr) << "Underlying buffer pointer shifted during move";
    
    // The pool size should remain unchanged (no unexpected returns or allocations)
    EXPECT_EQ(bufferPool->getFreeBuffers(), initialFree - 1);
}

// -------------------------------------------------------------------
// BUFFER MEMORY BOUNDARY & SLICING TESTS
// Proves the 128-byte prefix reservation is heavily guarded
// -------------------------------------------------------------------

TEST(BufferPoolTest, PrefixAndContentSliceBoundaries) {
    auto bufferPool = BufferPool::create();
    Mjpeg::Frame frame = bufferPool->acquire();
    auto* buffer = frame.getBuffer();

    // Insert mock hardware data
    std::vector<uint8_t> mockData = { 0xAA, 0xBB, 0xCC, 0xDD };
    buffer->insertContent(mockData);

    // Verify sizes
    EXPECT_EQ(buffer->contentSize(), mockData.size()) << "Content size calculation is incorrect";
    EXPECT_EQ(buffer->totalSize(), Mjpeg::Buffer::PREFIX_SIZE + mockData.size()) << "Total size does not account for reserved prefix";

    // Verify slice bounds
    auto contentSlice = buffer->getContentSlice();
    EXPECT_EQ(contentSlice.size(), mockData.size());
    EXPECT_EQ(contentSlice[0], 0xAA);

    auto prefixSlice = buffer->getPrefixSlice();
    EXPECT_EQ(prefixSlice.size(), Mjpeg::Buffer::PREFIX_SIZE);
}

TEST(BufferPoolTest, BufferTrimMaintainsPrefix) {
    auto bufferPool = BufferPool::create();
    Mjpeg::Frame frame = bufferPool->acquire();
    auto* buffer = frame.getBuffer();

    // Data representing a messy stream: [Garbage] [FF D8 ... FF D9] [Garbage]
    std::vector<uint8_t> streamData = { 0x00, 0x01, 0xFF, 0xD8, 0x4A, 0x50, 0xFF, 0xD9, 0x02, 0x03 };
    buffer->insertContent(streamData);

    // Simulate MjpegStream::outputFrame finding the SOI at index 2 and EOI at index 8
    size_t soiOffset = 2;
    size_t eoiOffset = 8;

    buffer->trim(soiOffset, eoiOffset);

    // Expected remaining content: { 0xFF, 0xD8, 0x4A, 0x50, 0xFF, 0xD9 }
    std::vector<uint8_t> expectedContent = { 0xFF, 0xD8, 0x4A, 0x50, 0xFF, 0xD9 };
    auto resultSlice = buffer->getContentSlice();

    EXPECT_EQ(buffer->contentSize(), expectedContent.size());
    EXPECT_THAT(
        std::vector<uint8_t>(resultSlice.begin(), resultSlice.end()),
        ::testing::ElementsAreArray(expectedContent)
    ) << "Trim algorithm corrupted the internal payload";

    // Crucially, verify the prefix still perfectly exists and wasn't destroyed by the vector erase
    EXPECT_EQ(buffer->getPrefixSlice().size(), Mjpeg::Buffer::PREFIX_SIZE) << "Trim operation corrupted the reserved prefix memory";
}

TEST(BufferPoolTest, BufferClearRestoresState) {
    auto bufferPool = BufferPool::create();
    Mjpeg::Buffer* underlyingPtr = nullptr;

    {
        Mjpeg::Frame frame = bufferPool->acquire();
        underlyingPtr = frame.getBuffer();
        
        // Insert data using the captured pointer
        std::vector<uint8_t> content = {0x01, 0x02, 0x03};
        underlyingPtr->insertContent(content);
        EXPECT_FALSE(underlyingPtr->empty());
    } // Frame dies, buffer is cleared and returned to pool via recycleFrameBridge

    // Re-acquire to get the exact same buffer back
    Mjpeg::Frame reusedFrame = bufferPool->acquire();
    EXPECT_EQ(reusedFrame.getBuffer(), underlyingPtr) << "Pool did not return the recycled buffer";
    
    // Verify clear() actually wiped the user data but kept the prefix allocation
    EXPECT_TRUE(reusedFrame.getBuffer()->empty()) << "Recycled buffer was not cleanly wiped";
    EXPECT_EQ(reusedFrame.getBuffer()->totalSize(), Mjpeg::Buffer::PREFIX_SIZE) << "Recycled buffer lost its prefix reservation";
}

// -------------------------------------------------------------------
// EXCEPTION & SAFETY TESTS
// -------------------------------------------------------------------

TEST(BufferPoolTest, OutOfBoundsTrimThrowsException) {
    auto bufferPool = BufferPool::create();
    Mjpeg::Frame frame = bufferPool->acquire();
    auto* buffer = frame.getBuffer();

    std::vector<uint8_t> content = { 0x01, 0x02, 0x03, 0x04 };
    buffer->insertContent(content);

    // Attempting to trim past the end of the content
    EXPECT_THROW({
        buffer->trim(1, 10);
    }, std::out_of_range) << "Failed to throw on end boundary violation";

    // Attempting to start the trim after the end boundary (logical impossibility)
    EXPECT_THROW({
        buffer->trim(3, 2);
    }, std::out_of_range) << "Failed to throw on inverted boundary violation";
}

TEST(BufferPoolTest, FrontOnEmptyBufferThrowsException) {
    auto bufferPool = BufferPool::create();
    Mjpeg::Frame frame = bufferPool->acquire();

    // The buffer is technically not "empty" vector-wise (it holds 128 bytes of prefix), 
    // but content-wise it is empty. front() should safely reject access.
    EXPECT_THROW({
        const auto* buffer = frame.getBuffer();
        buffer->front();
    }, std::out_of_range) << "Failed to throw when accessing front of empty payload";
}
