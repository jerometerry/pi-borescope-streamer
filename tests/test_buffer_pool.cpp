#include <algorithm>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <memory>
#include <utility>
#include <vector>
#include "buffer.hpp"
#include "buffer_pool.hpp"
#include "buffer_ptr.hpp"
#include "constants.hpp"
#include "intrusive_ptr.hpp"
#include "mjpeg_frame_queue.hpp"

static void pushFrame (MjpegFrameQueue& q, const std::shared_ptr<BufferPool>& bp, std::vector<uint8_t>& data) {
    BufferPtr frame = bp->borrow();
    frame->insertContent(data);
    q.push(frame);
};

TEST(BufferPoolTest, BoundedPoolGrowth) {
    auto bufferPool = BufferPool::create();
    MjpegFrameQueue frameQueue;

    std::vector<BufferPtr> slowConsumers;
    std::vector<uint8_t> dummyFrame = { 0xDE, 0xAD, 0xBE, 0xEF };

    constexpr int SPIKE_SIZE = 10;

    for (int i = 0; i < SPIKE_SIZE; ++i) {
        pushFrame(frameQueue, bufferPool, dummyFrame);
        uint32_t id = 0;
        slowConsumers.push_back(frameQueue.pop(id));
    }

    slowConsumers.clear();

    size_t currentPoolSize = bufferPool->getFreeBuffers();
    
    EXPECT_EQ(currentPoolSize, BufferPoolConfig::MAX_POOL_SIZE);
}

TEST(BufferPoolTest, DefaultTotalCapacity128K) {
    auto bufferPool = BufferPool::create();
    auto frame = bufferPool->borrow();
    auto expectedCapacity = Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES + BufferPoolConfig::BUFFER_PADDING;
    EXPECT_EQ(frame->totalCapacity(), expectedCapacity);
}

TEST(BufferPoolTest, FrameReserveAddsPaddingSize) {
    BufferPool::BufferPoolArgs args = {
        3, 1, Units::ONE_KILOBYTE
    };
    auto bufferPool = BufferPool::create(args);
    auto frame = bufferPool->borrow();
    auto expectedCapacity = args.bufferReserveSize + BufferPoolConfig::BUFFER_PADDING;
    EXPECT_EQ(frame->totalCapacity(), expectedCapacity);
}

TEST(BufferPoolTest, RawBufferPointerManipulation) {
    BufferPool::BufferPoolArgs args = {
        3, 1, Units::ONE_KILOBYTE
    };

    auto bufferPool = BufferPool::create(args);

    auto frame = bufferPool->borrow();
    auto* buffer = frame.get();

    constexpr std::string_view mjpegHeader = "--mjpegstream\r\nContent-Type: image/jpeg\r\n";

    // Get span<uinit_8> that covers the 128 byte reserved memory inside the buffer
    auto padding = buffer->getMutablePaddingSlice();

    // Initialize the entire padding memory with all zeros
    std::ranges::fill(padding, 0);

    // Convert the span into a raw pointer
    char* ptr = reinterpret_cast<char*>(padding.data());

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

    // For the expected data by concatenating the 128 byte padding with the 128 byte content
    std::vector<uint8_t> expectedData;
    expectedData.insert(expectedData.end(), headerData.begin(), headerData.end());
    expectedData.insert(expectedData.end(), contentData.begin(), contentData.end());

    // Get the read only span of the Buffer's underlying std::vector, which includes the 128 byte reserved prefix
    auto actualData = frame->all();

    EXPECT_THAT(
        actualData, 
        ::testing::ElementsAreArray(expectedData.begin(), expectedData.end())
    ) << "BufferPtr memory management error";
}

// -------------------------------------------------------------------
// FRAME REFERENCE COUNTING & LIFECYCLE TESTS
// Demonstrates safe zero-copy broadcasting across multiple simulated clients
// -------------------------------------------------------------------

TEST(BufferPoolTest, FrameReferenceCountingForMulticast) {
    BufferPool::BufferPoolArgs args {
        4, 4, 128
    };
    auto bufferPool = BufferPool::create(args);

    size_t initialFree = bufferPool->getFreeBuffers();

    {
        BufferPtr masterFrame = bufferPool->borrow();
        EXPECT_EQ(bufferPool->getFreeBuffers(), initialFree - 1) << "Buffer not removed from pool";

        {
            // Simulate Viewer 1 connecting and holding a copy of the frame
            BufferPtr viewer1Frame = masterFrame; 
            
            // Simulate Viewer 2 connecting and holding a copy
            BufferPtr viewer2Frame = masterFrame; 
            
            // Master frame is overwritten with a new frame from the camera
            masterFrame = bufferPool->borrow();
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
    BufferPool::BufferPoolArgs args {
        3, 1, 128
    };
    auto bufferPool = BufferPool::create(args);

    size_t initialFree = bufferPool->getFreeBuffers();

    BufferPtr original = bufferPool->borrow();
    Buffer* underlyingPtr = original.get();

    // Move the frame. This should transfer ownership without touching the atomic counter
    BufferPtr movedFrame = std::move(original);

    // The original frame should now be completely empty (operator bool() == false)

    bool is_valid = static_cast<bool>(original); // NOLINT(bugprone-use-after-move)
    EXPECT_FALSE(is_valid) << "Moved-from frame still holds a valid state";

    // The new frame should point to the exact same memory
    EXPECT_EQ(movedFrame.get(), underlyingPtr) << "Underlying buffer pointer shifted during move";
    
    // The pool size should remain unchanged (no unexpected returns or allocations)
    EXPECT_EQ(bufferPool->getFreeBuffers(), initialFree - 1);
}

// -------------------------------------------------------------------
// BUFFER MEMORY BOUNDARY & SLICING TESTS
// Proves the 128-byte padding reservation is heavily guarded
// -------------------------------------------------------------------

TEST(BufferPoolTest, PaddingAndContentSliceBoundaries) {
    BufferPool::BufferPoolArgs args {
        3, 1, 128
    };
    auto bufferPool = BufferPool::create(args);

    BufferPtr frame = bufferPool->borrow();
    auto* buffer = frame.get();

    // Insert mock hardware data
    std::vector<uint8_t> mockData = { 0xAA, 0xBB, 0xCC, 0xDD };
    buffer->insertContent(mockData);

    // Verify sizes
    EXPECT_EQ(buffer->contentSize(), mockData.size()) << "Content size calculation is incorrect";
    EXPECT_EQ(buffer->totalSize(), BufferPoolConfig::BUFFER_PADDING + mockData.size()) << "Total size does not account for reserved prefix";

    // Verify slice bounds
    auto contentSlice = buffer->getContentSlice();
    EXPECT_EQ(contentSlice.size(), mockData.size());
    EXPECT_EQ(contentSlice[0], 0xAA);

    auto paddingSlice = buffer->getPaddingSlice();
    EXPECT_EQ(paddingSlice.size(), BufferPoolConfig::BUFFER_PADDING);
}

TEST(BufferPoolTest, BufferTrimMaintainsPadding) {
    BufferPool::BufferPoolArgs args {
        3, 1, 128
    };
    auto bufferPool = BufferPool::create(args);

    BufferPtr frame = bufferPool->borrow();
    auto* buffer = frame.get();

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

    // Verify the padding still exists and wasn't destroyed by the vector erase
    EXPECT_EQ(
        buffer->getPaddingSlice().size(), 
        BufferPoolConfig::BUFFER_PADDING
    ) << "Trim operation corrupted the reserved padding memory";
}

TEST(BufferPoolTest, BufferClearRestoresState) {
    BufferPool::BufferPoolArgs args {
        3, 1, 128
    };
    auto bufferPool = BufferPool::create(args);

    Buffer* underlyingPtr = nullptr;

    {
        BufferPtr frame = bufferPool->borrow();
        underlyingPtr = frame.get();
        
        // Insert data using the captured pointer
        std::vector<uint8_t> content = {0x01, 0x02, 0x03};
        underlyingPtr->insertContent(content);
        EXPECT_FALSE(underlyingPtr->empty());
    } // BufferPtr dies, buffer is cleared and returned to pool

    // Re-acquire to get the exact same buffer back
    BufferPtr reusedFrame = bufferPool->borrow();
    EXPECT_EQ(reusedFrame.get(), underlyingPtr) << "Pool did not return the recycled buffer";
    
    // Verify clear() actually wiped the user data but kept the prefix allocation
    EXPECT_TRUE(reusedFrame.get()->empty()) << "Recycled buffer was not cleanly wiped";
    EXPECT_EQ(reusedFrame.get()->totalSize(), BufferPoolConfig::BUFFER_PADDING) << "Recycled buffer lost its prefix reservation";
}

// -------------------------------------------------------------------
// EXCEPTION & SAFETY TESTS
// -------------------------------------------------------------------

TEST(BufferPoolTest, OutOfBoundsTrimThrowsException) {
    BufferPool::BufferPoolArgs args {
        3, 1, 128
    };
    auto bufferPool = BufferPool::create(args);
    BufferPtr frame = bufferPool->borrow();
    auto* buffer = frame.get();

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
    BufferPool::BufferPoolArgs args {
        3, 1, 128
    };
    auto bufferPool = BufferPool::create(args);
    BufferPtr frame = bufferPool->borrow();

    // The buffer is technically not "empty" vector-wise (it holds 128 bytes of prefix), 
    // but content-wise it is empty. front() should safely reject access.
    EXPECT_THROW({
        const auto* buffer = frame.get();
        buffer->front();
    }, std::out_of_range) << "Failed to throw when accessing front of empty payload";
}

TEST(BufferPoolTest, BufferPointerMath_InsertContentDoesNotChangePointer) {
    BufferPool::BufferPoolArgs args {
        3, 1, 128
    };
    auto bufferPool = BufferPool::create(args);
    auto frame = bufferPool->borrow();

    auto* buffer = frame.get();
    std::vector<uint8_t> payload = { 0xDE, 0xAD, 0xBE, 0xEF };

    char* paddingStartPtr1 = reinterpret_cast<char*>(frame->getMutablePaddingSlice().data());
    char* contentStartPtr1 = reinterpret_cast<char*>(frame->getMutableContentSlice().data());

    EXPECT_EQ(buffer->getMutableContentSlice().size(), 0);
    EXPECT_EQ(buffer->getMutablePaddingSlice().size(), 128);

    frame->insertContent(payload);

    EXPECT_EQ(buffer->getMutableContentSlice().size(), 4);
    EXPECT_EQ(buffer->getMutablePaddingSlice().size(), 128);

    char* paddingStartPtr2 = reinterpret_cast<char*>(frame->getMutablePaddingSlice().data());
    char* contentStartPtr2 = reinterpret_cast<char*>(frame->getMutableContentSlice().data());

    EXPECT_EQ(paddingStartPtr1, paddingStartPtr2);
    EXPECT_EQ(contentStartPtr1, contentStartPtr2);
}

TEST(BufferPoolTest, BufferPointerMath_ContentPtrOffsetFromPaddingPtr) {
    BufferPool::BufferPoolArgs args {
        3, 1, 128
    };
    auto bufferPool = BufferPool::create(args);
    auto frame = bufferPool->borrow();

    auto* buffer = frame.get();
    std::vector<uint8_t> payload = { 0xDE, 0xAD, 0xBE, 0xEF };

    frame->insertContent(payload);
 
    const char* contentStartPtr = reinterpret_cast<const char*>(buffer->getMutableContentSlice().data());
    const char* paddingStartPtr = reinterpret_cast<const char*>(buffer->getMutablePaddingSlice().data());

    auto expectedContentStartPtr = paddingStartPtr + buffer->paddingSize();
    EXPECT_EQ(contentStartPtr, expectedContentStartPtr);
}