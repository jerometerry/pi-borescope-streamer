#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include "chunk_metadata.hpp"
#include "usb_frame_decoder.hpp"
#include "usb_packet_header.hpp"

class MockHandlers {
public:
    MOCK_METHOD(void, OnBroadcast, (const std::vector<uint8_t>& frameData));
    MOCK_METHOD(void, OnButtonPress, ());
};

class UsbFrameDecoderTest : public ::testing::Test {
private:
    MockHandlers mock_handlers_;
    std::unique_ptr<UsbFrameDecoder> decoder_;

protected:
    void SetUp() override {
        decoder_ = std::make_unique<UsbFrameDecoder>(
            [this](const std::vector<uint8_t>& data) { mock_handlers_.OnBroadcast(data); },
            [this]() { mock_handlers_.OnButtonPress(); }
        );
    }

    MockHandlers& GetMock() { return mock_handlers_; }
    UsbFrameDecoder& GetDecoder() { return *decoder_; }
};

TEST_F(UsbFrameDecoderTest, ExtractsPhysicalBufferIgnoringDeclaredLength) {
    size_t headerSize = sizeof(UsbPacketHeader) + sizeof(ChunkMetadata);
    std::vector<uint8_t> hardwarePacket(1024, 0xDD); 
    
    auto* usb = reinterpret_cast<UsbPacketHeader*>(hardwarePacket.data());
    usb->header = 0xBBAA; usb->cameraId = 11; usb->length = 939;

    auto* chunk = reinterpret_cast<ChunkMetadata*>(hardwarePacket.data() + sizeof(UsbPacketHeader));
    chunk->frameId = 2; chunk->cameraNumber = 0; chunk->flags = 0; chunk->gravitySensor = 0;    

    hardwarePacket[headerSize] = 0xFF;
    hardwarePacket[headerSize + 1] = 0xD8;
    hardwarePacket[sizeof(UsbPacketHeader) + usb->length - 2] = 0xFF;
    hardwarePacket[sizeof(UsbPacketHeader) + usb->length - 1] = 0xD9;

    std::vector<uint8_t> triggerFrame = hardwarePacket; 
    auto* triggerChunk = reinterpret_cast<ChunkMetadata*>(triggerFrame.data() + sizeof(UsbPacketHeader));
    triggerChunk->frameId = 3;

    std::vector<uint8_t> expectedPayload(
        hardwarePacket.begin() + sizeof(UsbPacketHeader) + sizeof(ChunkMetadata),
        hardwarePacket.begin() + sizeof(UsbPacketHeader) + usb->length
    );
    EXPECT_CALL(GetMock(), OnBroadcast(expectedPayload)).Times(1);

    GetDecoder().processIncomingCameraData(hardwarePacket);
    GetDecoder().processIncomingCameraData(triggerFrame);
}

TEST_F(UsbFrameDecoderTest, SafelyIgnoresHardwareTailChunks) {
    std::vector<uint8_t> validHeader(1024, 0x00);
    auto* usb = reinterpret_cast<UsbPacketHeader*>(validHeader.data());
    usb->header = 0xBBAA; usb->cameraId = 11; usb->length = 1024 - sizeof(UsbPacketHeader);

    auto* chunk = reinterpret_cast<ChunkMetadata*>(validHeader.data() + sizeof(UsbPacketHeader));
    chunk->frameId = 1; chunk->cameraNumber = 0; chunk->flags = 0; chunk->gravitySensor = 0;    

    size_t payloadOffset = sizeof(UsbPacketHeader) + sizeof(ChunkMetadata);
    validHeader[payloadOffset] = 0xFF;
    validHeader[payloadOffset + 1] = 0xD8;
    validHeader[validHeader.size() - 2] = 0xFF;
    validHeader[validHeader.size() - 1] = 0xD9;

    std::vector<uint8_t> shortPacketTail(80, 0xFF);

    std::vector<uint8_t> triggerFrame = validHeader;
    auto* triggerChunk = reinterpret_cast<ChunkMetadata*>(triggerFrame.data() + sizeof(UsbPacketHeader));
    triggerChunk->frameId = 2;   

    std::vector<uint8_t> expectedPayload(
        validHeader.begin() + payloadOffset,
        validHeader.end()
    );
    EXPECT_CALL(GetMock(), OnBroadcast(expectedPayload)).Times(1);

    GetDecoder().processIncomingCameraData(validHeader);
    GetDecoder().processIncomingCameraData(shortPacketTail); // Should be safely ignored
    GetDecoder().processIncomingCameraData(triggerFrame);
}


TEST_F(UsbFrameDecoderTest, ReassemblesMultiChunkMjpegStream) {
    std::vector<uint8_t> expectedPayload;

    auto buildPacket = [](uint8_t frameId, const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> packet(sizeof(UsbPacketHeader) + sizeof(ChunkMetadata) + payload.size(), 0x00);
        
        auto* usb = reinterpret_cast<UsbPacketHeader*>(packet.data());
        usb->header = 0xBBAA;
        usb->cameraId = 11;
        usb->length = sizeof(ChunkMetadata) + payload.size();

        auto* chunk = reinterpret_cast<ChunkMetadata*>(packet.data() + sizeof(UsbPacketHeader));
        chunk->frameId = frameId;
        chunk->cameraNumber = 0;
        chunk->flags = 0;
        chunk->gravitySensor = 0;
        
        std::copy(payload.begin(), payload.end(), packet.begin() + sizeof(UsbPacketHeader) + sizeof(ChunkMetadata));
        
        return packet;
    };

    std::vector<uint8_t> payload1 = {0xFF, 0xD8, 0x01, 0x02};
    expectedPayload.insert(expectedPayload.end(), payload1.begin(), payload1.end());
    auto packet1 = buildPacket(1, payload1);

    std::vector<uint8_t> payload2 = {0x03, 0x04, 0x05, 0x06, 0xFF, 0xD9};
    expectedPayload.insert(expectedPayload.end(), payload2.begin(), payload2.end());
    auto packet2 = buildPacket(1, payload2);

    auto packet3 = buildPacket(2, {0xFF, 0xD8, 0xAA, 0xBB});

    EXPECT_CALL(GetMock(), OnBroadcast(expectedPayload)).Times(1);

    GetDecoder().processIncomingCameraData(packet1);
    GetDecoder().processIncomingCameraData(packet2);
    GetDecoder().processIncomingCameraData(packet3);
}

TEST_F(UsbFrameDecoderTest, TriggersButtonHandlerOnHardwareFlag) {
    auto buildPacket = [](uint8_t frameId, bool isButtonPressed, const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> packet(sizeof(UsbPacketHeader) + sizeof(ChunkMetadata) + payload.size(), 0x00);
        
        auto* usb = reinterpret_cast<UsbPacketHeader*>(packet.data());
        usb->header = 0xBBAA;
        usb->cameraId = 11;
        usb->length = sizeof(ChunkMetadata) + payload.size();

        auto* chunk = reinterpret_cast<ChunkMetadata*>(packet.data() + sizeof(UsbPacketHeader));
        chunk->frameId = frameId;
        chunk->cameraNumber = 0;
        chunk->flags = 0;
        chunk->gravitySensor = 0;
        chunk->setButtonPressed(isButtonPressed);
        
        std::copy(payload.begin(), payload.end(), packet.begin() + sizeof(UsbPacketHeader) + sizeof(ChunkMetadata));
        
        return packet;
    };

    EXPECT_CALL(GetMock(), OnButtonPress()).Times(1);
    EXPECT_CALL(GetMock(), OnBroadcast(::testing::_)).Times(::testing::AnyNumber());

    auto packet1 = buildPacket(1, false, {0xFF, 0xD8, 0x01});
    GetDecoder().processIncomingCameraData(packet1);

    auto packet2 = buildPacket(1, true, {0x02, 0x03, 0x04});
    GetDecoder().processIncomingCameraData(packet2);

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
    chunk->cameraNumber = 0;
    chunk->flags = 0;
    chunk->gravitySensor = 0;
    chunk->setButtonPressed(true);

    EXPECT_CALL(GetMock(), OnButtonPress()).Times(1);
    GetDecoder().processIncomingCameraData(packet);
}

TEST_F(UsbFrameDecoderTest, AccumulatesDataAndEmitsOnFrameIdChange) {
    std::vector<uint8_t> packet1(100, 0x00);
    auto* usb1 = reinterpret_cast<UsbPacketHeader*>(packet1.data());
    usb1->header = 0xBBAA; usb1->cameraId = 11; usb1->length = 50; 

    auto* chunk1 = reinterpret_cast<ChunkMetadata*>(packet1.data() + sizeof(UsbPacketHeader));
    chunk1->frameId = 1; chunk1->cameraNumber = 0; chunk1->gravitySensor = 0;

    size_t const payload_offset = sizeof(UsbPacketHeader) + sizeof(ChunkMetadata);
    std::fill(packet1.begin() + payload_offset, packet1.begin() + usb1->length, 0xDE);
    packet1[payload_offset] = 0xFF;      // SOI
    packet1[payload_offset + 1] = 0xD8;
    packet1[sizeof(UsbPacketHeader) + usb1->length - 2] = 0xFF; // EOI
    packet1[sizeof(UsbPacketHeader) + usb1->length - 1] = 0xD9;

    std::vector<uint8_t> packet2(100, 0x00);
    auto* usb2 = reinterpret_cast<UsbPacketHeader*>(packet2.data());
    usb2->header = 0xBBAA; usb2->cameraId = 11; usb2->length = 50;

    auto* chunk2 = reinterpret_cast<ChunkMetadata*>(packet2.data() + sizeof(UsbPacketHeader));
    chunk2->frameId = 2; chunk2->cameraNumber = 0; chunk2->gravitySensor = 0;
    std::fill(packet2.begin() + payload_offset, packet2.begin() + usb2->length, 0xAA);

    EXPECT_CALL(GetMock(), OnBroadcast(::testing::Property(&std::vector<uint8_t>::empty, false)))
        .WillOnce(::testing::Invoke([](const std::vector<uint8_t>& frameBuffer) {
            ASSERT_FALSE(frameBuffer.empty());
            EXPECT_EQ(frameBuffer.front(), 0xFF); // Front byte is now the verified SOI marker
        }));

    GetDecoder().processIncomingCameraData(packet1);
    GetDecoder().processIncomingCameraData(packet2);
}

TEST_F(UsbFrameDecoderTest, IgnoresInvalidCameraId) {
    std::vector<uint8_t> packet(20, 0x00);
    auto* usb = reinterpret_cast<UsbPacketHeader*>(packet.data());
    usb->header = 0xBBAA;
    usb->cameraId = 99;
    usb->length = 15;

    EXPECT_CALL(GetMock(), OnBroadcast(::testing::_)).Times(0);
    GetDecoder().processIncomingCameraData(packet);
}

TEST_F(UsbFrameDecoderTest, IgnoresPayloadExceedingBufferSize) {
    std::vector<uint8_t> packet(10, 0x00); 
    auto* usb = reinterpret_cast<UsbPacketHeader*>(packet.data());
    usb->header = 0xBBAA;
    usb->cameraId = 11;
    usb->length = 50; 

    EXPECT_CALL(GetMock(), OnBroadcast(::testing::_)).Times(0);
    GetDecoder().processIncomingCameraData(packet);
}

TEST_F(UsbFrameDecoderTest, IgnoresTruncatedMetadata) {
    std::vector<uint8_t> packet(10, 0x00); 
    auto* usb = reinterpret_cast<UsbPacketHeader*>(packet.data());
    usb->header = 0xBBAA;
    usb->cameraId = 11;
    usb->length = 5; 
    
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
    chunk->cameraNumber = 5;
    chunk->flags = 0;
    chunk->gravitySensor = 0;

    EXPECT_CALL(GetMock(), OnBroadcast(::testing::_)).Times(0);
    GetDecoder().processIncomingCameraData(packet);
}

TEST_F(UsbFrameDecoderTest, AbortsOnMidFrameCameraShift) {
    std::vector<uint8_t> packet1(20, 0x00);
    auto* usb1 = reinterpret_cast<UsbPacketHeader*>(packet1.data());
    usb1->header = 0xBBAA;
    usb1->cameraId = 11;
    usb1->length = 15;
    auto* chunk1 = reinterpret_cast<ChunkMetadata*>(packet1.data() + sizeof(UsbPacketHeader));
    chunk1->frameId = 1;
    chunk1->cameraNumber = 0;
    chunk1->gravitySensor = 0;
    
    GetDecoder().processIncomingCameraData(packet1);

    std::vector<uint8_t> packet2(20, 0x00);
    auto* usb2 = reinterpret_cast<UsbPacketHeader*>(packet2.data());
    usb2->header = 0xBBAA;
    usb2->cameraId = 11;
    usb2->length = 15;
    auto* chunk2 = reinterpret_cast<ChunkMetadata*>(packet2.data() + sizeof(UsbPacketHeader));
    chunk2->frameId = 1; 
    chunk2->cameraNumber = 1;
    chunk2->gravitySensor = 0;

    EXPECT_CALL(GetMock(), OnBroadcast(::testing::_)).Times(0);
    GetDecoder().processIncomingCameraData(packet2);
}

TEST(UsbFrameDecoderEdgeTest, HandlesNullCallbacksSafely) {
    UsbFrameDecoder silentDecoder(nullptr, nullptr);
    std::vector<uint8_t> packet(100, 0x00);
    ASSERT_NO_THROW(silentDecoder.processIncomingCameraData(packet));
}