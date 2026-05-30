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