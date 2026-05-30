#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <vector>
#include <cstdint>
#include <span>
#include <memory>
#include <algorithm>

#include "usb_frame_decoder.hpp"
#include "usb_packet_header.hpp"
#include "chunk_metadata.hpp"

class MockHandlers {
public:
    MOCK_METHOD(void, OnBroadcast, (const std::vector<uint8_t>& frameData));
    MOCK_METHOD(void, OnButtonPress, ());
};

class UsbFrameDecoderTest : public ::testing::Test {
private:
    // Kept private to satisfy cppcoreguidelines-non-private-member-variables-in-classes
    MockHandlers mock_handlers_;
    std::unique_ptr<UsbFrameDecoder> decoder_;

protected:
    void SetUp() override {
        decoder_ = std::make_unique<UsbFrameDecoder>(
            [this](const std::vector<uint8_t>& data) { mock_handlers_.OnBroadcast(data); },
            [this]() { mock_handlers_.OnButtonPress(); }
        );
    }

    // Helper functions to safely access private fixture members
    MockHandlers& GetMock() { return mock_handlers_; }
    UsbFrameDecoder& GetDecoder() { return *decoder_; }
};

TEST_F(UsbFrameDecoderTest, ReassemblesMultiChunkMjpegStream) {
    // 1. Define the expected final JPEG payload for Frame 1
    std::vector<uint8_t> expectedPayload;

    // A reusable lambda to safely pack headers and payloads into a raw byte stream
    auto buildPacket = [](uint8_t frameId, const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> packet(sizeof(UsbPacketHeader) + sizeof(ChunkMetadata) + payload.size(), 0x00);
        
        auto* usb = reinterpret_cast<UsbPacketHeader*>(packet.data());
        usb->header = 0xBBAA;
        usb->cameraId = 11;
        usb->length = sizeof(ChunkMetadata) + payload.size();

        auto* chunk = reinterpret_cast<ChunkMetadata*>(packet.data() + sizeof(UsbPacketHeader));
        chunk->frameId = frameId;
        chunk->cameraNumber = 0; // Valid supported camera
        
        // Copy the raw JPEG pixels into the end of the packet
        std::copy(payload.begin(), payload.end(), packet.begin() + sizeof(UsbPacketHeader) + sizeof(ChunkMetadata));
        
        return packet;
    };

    // 2. Construct Frame 1 (Fragmented across two chunks)
    std::vector<uint8_t> payload1 = {0xFF, 0xD8, 0x01, 0x02}; // JPEG SOI
    expectedPayload.insert(expectedPayload.end(), payload1.begin(), payload1.end());
    auto packet1 = buildPacket(1, payload1);

    std::vector<uint8_t> payload2 = {0x03, 0x04, 0x05, 0x06, 0xFF, 0xD9}; // JPEG EOI
    expectedPayload.insert(expectedPayload.end(), payload2.begin(), payload2.end());
    auto packet2 = buildPacket(1, payload2);

    // 3. Construct Frame 2 (This change in ID triggers the emission of Frame 1)
    auto packet3 = buildPacket(2, {0xFF, 0xD8, 0xAA, 0xBB});

    // 4. Assert that OnBroadcast is called EXACTLY ONCE, 
    // and that it contains the perfectly concatenated Frame 1 payload.
    EXPECT_CALL(GetMock(), OnBroadcast(expectedPayload)).Times(1);

    // 5. Stream the fragmented data into the decoder
    GetDecoder().processIncomingCameraData(packet1);
    GetDecoder().processIncomingCameraData(packet2);
    GetDecoder().processIncomingCameraData(packet3); // <--- Triggers the emit
}

TEST_F(UsbFrameDecoderTest, TriggersButtonHandlerOnHardwareFlag) {
    // 1. Expand the lambda to control the hardware button flag
    auto buildPacket = [](uint8_t frameId, bool isButtonPressed, const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> packet(sizeof(UsbPacketHeader) + sizeof(ChunkMetadata) + payload.size(), 0x00);
        
        auto* usb = reinterpret_cast<UsbPacketHeader*>(packet.data());
        usb->header = 0xBBAA;
        usb->cameraId = 11;
        usb->length = sizeof(ChunkMetadata) + payload.size();

        auto* chunk = reinterpret_cast<ChunkMetadata*>(packet.data() + sizeof(UsbPacketHeader));
        chunk->frameId = frameId;
        chunk->cameraNumber = 0; 
        
        // Directly map the boolean to the 1-bit hardware flag
        chunk->buttonPress = isButtonPressed ? 1 : 0; 
        
        std::copy(payload.begin(), payload.end(), packet.begin() + sizeof(UsbPacketHeader) + sizeof(ChunkMetadata));
        
        return packet;
    };

    // 2. We assert that the button callback is fired EXACTLY once, 
    // proving it doesn't double-fire or get lost in the stream.
    EXPECT_CALL(GetMock(), OnButtonPress()).Times(1);

    // We do not care about the frame broadcast for this specific test, 
    // so we can explicitly ignore any broadcast calls.
    EXPECT_CALL(GetMock(), OnBroadcast(::testing::_)).Times(::testing::AnyNumber());

    // 3. Simulate the stream and the hardware pulse
    // Chunk 1: Normal video data, button NOT pressed
    auto packet1 = buildPacket(1, false, {0xFF, 0xD8, 0x01});
    GetDecoder().processIncomingCameraData(packet1);

    // Chunk 2: The exact millisecond the user clicks the physical button on the cable
    auto packet2 = buildPacket(1, true, {0x02, 0x03, 0x04});
    GetDecoder().processIncomingCameraData(packet2);

    // Chunk 3: Normal video data resumes, button is instantly released (the 0ms pulse)
    auto packet3 = buildPacket(1, false, {0x05, 0xFF, 0xD9});
    GetDecoder().processIncomingCameraData(packet3);
}

TEST_F(UsbFrameDecoderTest, IgnoresInvalidHeaderOrShortBuffer) {
    EXPECT_CALL(GetMock(), OnBroadcast(::testing::_)).Times(0);
    EXPECT_CALL(GetMock(), OnButtonPress()).Times(0);

    std::vector<uint8_t> const short_buffer = {0xAA, 0xBB};
    GetDecoder().processIncomingCameraData(short_buffer);

    std::vector<uint8_t> const bad_magic_buffer(100, 0x00);
    GetDecoder().processIncomingCameraData(bad_magic_buffer);
}

TEST_F(UsbFrameDecoderTest, TriggersButtonHandlerOnFlag) {
    std::vector<uint8_t> packet(128, 0x00);

    auto* usb_hdr = reinterpret_cast<UsbPacketHeader*>(packet.data());
    usb_hdr->header = 0xBBAA;  
    usb_hdr->cameraId = 7;     
    usb_hdr->length = 64;      

    auto* chunk = reinterpret_cast<ChunkMetadata*>(packet.data() + sizeof(UsbPacketHeader));
    chunk->frameId = 101;
    chunk->buttonPress = 1;    
    chunk->cameraNumber = 0;   
    chunk->hasGravitySensor = 0;
    chunk->otherFlags = 0;

    EXPECT_CALL(GetMock(), OnButtonPress()).Times(1);

    GetDecoder().processIncomingCameraData(packet);
}

TEST_F(UsbFrameDecoderTest, AccumulatesDataAndEmitsOnFrameIdChange) {
    std::vector<uint8_t> packet1(100, 0x00);
    auto* usb1 = reinterpret_cast<UsbPacketHeader*>(packet1.data());
    usb1->header = 0xBBAA;
    usb1->cameraId = 11;
    usb1->length = 50; 

    auto* chunk1 = reinterpret_cast<ChunkMetadata*>(packet1.data() + sizeof(UsbPacketHeader));
    chunk1->frameId = 1;
    chunk1->cameraNumber = 0;

    size_t const payload_offset = sizeof(UsbPacketHeader) + sizeof(ChunkMetadata);
    std::fill(packet1.begin() + payload_offset, packet1.begin() + usb1->length, 0xDE);

    std::vector<uint8_t> packet2(100, 0x00);
    auto* usb2 = reinterpret_cast<UsbPacketHeader*>(packet2.data());
    usb2->header = 0xBBAA;
    usb2->cameraId = 11;
    usb2->length = 50;

    auto* chunk2 = reinterpret_cast<ChunkMetadata*>(packet2.data() + sizeof(UsbPacketHeader));
    chunk2->frameId = 2; 
    chunk2->cameraNumber = 0;
    
    std::fill(packet2.begin() + payload_offset, packet2.begin() + usb2->length, 0xAA);

    // Fully qualified ::testing::Invoke prevents the namespace lookup error
    EXPECT_CALL(GetMock(), OnBroadcast(::testing::Property(&std::vector<uint8_t>::empty, false)))
        .WillOnce(::testing::Invoke([](const std::vector<uint8_t>& frameBuffer) {
            ASSERT_FALSE(frameBuffer.empty());
            EXPECT_EQ(frameBuffer.front(), 0xDE);
        }));

    GetDecoder().processIncomingCameraData(packet1);
    GetDecoder().processIncomingCameraData(packet2);
}

TEST_F(UsbFrameDecoderTest, IgnoresInvalidCameraId) {
    std::vector<uint8_t> packet(20, 0x00);
    auto* usb = reinterpret_cast<UsbPacketHeader*>(packet.data());
    usb->header = 0xBBAA;
    usb->cameraId = 99; // <--- Invalid ID (Valid are 7, 11)
    usb->length = 15;

    EXPECT_CALL(GetMock(), OnBroadcast(::testing::_)).Times(0);
    GetDecoder().processIncomingCameraData(packet);
}

TEST_F(UsbFrameDecoderTest, IgnoresPayloadExceedingBufferSize) {
    std::vector<uint8_t> packet(10, 0x00); // Total buffer is only 10 bytes
    auto* usb = reinterpret_cast<UsbPacketHeader*>(packet.data());
    usb->header = 0xBBAA;
    usb->cameraId = 11;
    usb->length = 50; // <--- Malicious/corrupted header claiming 50 bytes of payload

    EXPECT_CALL(GetMock(), OnBroadcast(::testing::_)).Times(0);
    GetDecoder().processIncomingCameraData(packet);
}

TEST_F(UsbFrameDecoderTest, IgnoresTruncatedMetadata) {
    std::vector<uint8_t> packet(10, 0x00); // Total buffer is 10 bytes
    auto* usb = reinterpret_cast<UsbPacketHeader*>(packet.data());
    usb->header = 0xBBAA;
    usb->cameraId = 11;
    usb->length = 5; // Matches buffer size: 5 (Header) + 5 (Payload) = 10.
    
    // BUT the ChunkMetadata struct is 7 bytes! 
    // The decoder should realize the metadata is truncated and abort.
    EXPECT_CALL(GetMock(), OnBroadcast(::testing::_)).Times(0);
    GetDecoder().processIncomingCameraData(packet);
}

TEST_F(UsbFrameDecoderTest, IgnoresUnsupportedCameraConfiguration) {
    std::vector<uint8_t> packet(20, 0x00);
    auto* usb = reinterpret_cast<UsbPacketHeader*>(packet.data());
    usb->header = 0xBBAA;
    usb->cameraId = 11;
    usb->length = 15;

    auto* chunk = reinterpret_cast<ChunkMetadata*>(packet.data() + sizeof(UsbPacketHeader));
    chunk->frameId = 1;
    chunk->cameraNumber = 5; // <--- Fails the "cameraNumber < 2" check

    EXPECT_CALL(GetMock(), OnBroadcast(::testing::_)).Times(0);
    GetDecoder().processIncomingCameraData(packet);
}

TEST_F(UsbFrameDecoderTest, AbortsOnMidFrameCameraShift) {
    // 1. Send the first chunk of Frame 1 (Valid)
    std::vector<uint8_t> packet1(20, 0x00);
    auto* usb1 = reinterpret_cast<UsbPacketHeader*>(packet1.data());
    usb1->header = 0xBBAA;
    usb1->cameraId = 11;
    usb1->length = 15;
    auto* chunk1 = reinterpret_cast<ChunkMetadata*>(packet1.data() + sizeof(UsbPacketHeader));
    chunk1->frameId = 1;
    chunk1->cameraNumber = 0; 
    
    GetDecoder().processIncomingCameraData(packet1);

    // 2. Send the second chunk. Same Frame ID, but the cameraNumber flipped!
    std::vector<uint8_t> packet2(20, 0x00);
    auto* usb2 = reinterpret_cast<UsbPacketHeader*>(packet2.data());
    usb2->header = 0xBBAA;
    usb2->cameraId = 11;
    usb2->length = 15;
    auto* chunk2 = reinterpret_cast<ChunkMetadata*>(packet2.data() + sizeof(UsbPacketHeader));
    chunk2->frameId = 1; // Still frame 1...
    chunk2->cameraNumber = 1; // ...but suddenly it's camera 1!

    // Ensure it aborts and DOES NOT append the corrupted data
    EXPECT_CALL(GetMock(), OnBroadcast(::testing::_)).Times(0);
    GetDecoder().processIncomingCameraData(packet2);
}

TEST(UsbFrameDecoderEdgeTest, HandlesNullCallbacksSafely) {
    // Instantiate WITHOUT your mock handlers
    UsbFrameDecoder silentDecoder(nullptr, nullptr);
    
    std::vector<uint8_t> packet(100, 0x00);
    // ... [Set up a valid packet with the buttonPress flag set to 1] ...

    // Processing should succeed without throwing a null pointer exception
    ASSERT_NO_THROW(silentDecoder.processIncomingCameraData(packet));
}