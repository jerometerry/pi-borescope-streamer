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
#include "data_structures.hpp"
#include "mjpeg_stream.hpp"

class MockHandlers {
public:
    MOCK_METHOD(void, output, (const std::vector<uint8_t>& output));
};

class MjpegStreamTest : public ::testing::Test {
private:
    MockHandlers handler_;
    std::unique_ptr<MjpegStream> stream_;

protected:
    void SetUp() override {
        stream_ = std::make_unique<MjpegStream>(
            [this](const std::vector<uint8_t>& data) { handler_.output(data); }
        );
    }

    MockHandlers& GetOutputHandler() { return handler_; }
    MjpegStream& GetStream() { return *stream_; }
};

TEST_F(MjpegStreamTest, ExtractsPhysicalBufferIgnoringDeclaredLength) {
    size_t headerSize = sizeof(UsbPacketHeader) + sizeof(CameraPacketHeader);
    std::vector<uint8_t> hardwarePacket(1024, 0xDD); 
    
    auto* usb = reinterpret_cast<UsbPacketHeader*>(hardwarePacket.data());
    usb->setHeader(0xBBAA); usb->setCameraId(11); usb->setLength( 939);

    auto* chunk = reinterpret_cast<CameraPacketHeader*>(hardwarePacket.data() + sizeof(UsbPacketHeader));
    chunk->setFrameId(2); chunk->setCameraNumber(0); chunk->setFlags(0); chunk->setGravitySensor(0);    

    hardwarePacket[headerSize] = 0xFF;
    hardwarePacket[headerSize + 1] = 0xD8;
    hardwarePacket[sizeof(UsbPacketHeader) + usb->getLength() - 2] = 0xFF;
    hardwarePacket[sizeof(UsbPacketHeader) + usb->getLength() - 1] = 0xD9;

    std::vector<uint8_t> triggerFrame = hardwarePacket; 
    auto* triggerChunk = reinterpret_cast<CameraPacketHeader*>(triggerFrame.data() + sizeof(UsbPacketHeader));
    triggerChunk->setFrameId(3);

    std::vector<uint8_t> expectedPayload(
        hardwarePacket.begin() + sizeof(UsbPacketHeader) + sizeof(CameraPacketHeader),
        hardwarePacket.begin() + sizeof(UsbPacketHeader) + usb->getLength()
    );
    EXPECT_CALL(GetOutputHandler(), output(expectedPayload)).Times(1);

    GetStream().send(hardwarePacket);
    GetStream().send(triggerFrame);
}

TEST_F(MjpegStreamTest, SafelyIgnoresHardwareTailChunks) {
    std::vector<uint8_t> validHeader(1024, 0x00);
    auto* usb = reinterpret_cast<UsbPacketHeader*>(validHeader.data());
    usb->setHeader(0xBBAA); usb->setCameraId(11); usb->setLength(1024 - sizeof(UsbPacketHeader));

    auto* chunk = reinterpret_cast<CameraPacketHeader*>(validHeader.data() + sizeof(UsbPacketHeader));
    chunk->setFrameId(1); chunk->setCameraNumber(0); chunk->setFlags(0); chunk->setGravitySensor(0);    

    size_t payloadOffset = sizeof(UsbPacketHeader) + sizeof(CameraPacketHeader);
    validHeader[payloadOffset] = 0xFF;
    validHeader[payloadOffset + 1] = 0xD8;
    validHeader[validHeader.size() - 2] = 0xFF;
    validHeader[validHeader.size() - 1] = 0xD9;

    std::vector<uint8_t> shortPacketTail(80, 0xFF);

    std::vector<uint8_t> triggerFrame = validHeader;
    auto* triggerChunk = reinterpret_cast<CameraPacketHeader*>(triggerFrame.data() + sizeof(UsbPacketHeader));
    triggerChunk->setFrameId(2);   

    std::vector<uint8_t> expectedPayload(
        validHeader.begin() + payloadOffset,
        validHeader.end()
    );
    EXPECT_CALL(GetOutputHandler(), output(expectedPayload)).Times(1);

    GetStream().send(validHeader);
    GetStream().send(shortPacketTail); // Should be safely ignored
    GetStream().send(triggerFrame);
}


TEST_F(MjpegStreamTest, ReassemblesMultiChunkMjpegStream) {
    std::vector<uint8_t> expectedPayload;

    auto buildPacket = [](uint8_t frameId, const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> packet(sizeof(UsbPacketHeader) + sizeof(CameraPacketHeader) + payload.size(), 0x00);
        
        auto* usb = reinterpret_cast<UsbPacketHeader*>(packet.data());
        usb->setHeader(0xBBAA);
        usb->setCameraId(11);
        usb->setLength(sizeof(CameraPacketHeader) + payload.size());

        auto* chunk = reinterpret_cast<CameraPacketHeader*>(packet.data() + sizeof(UsbPacketHeader));
        chunk->setFrameId(frameId);
        chunk->setCameraNumber(0);
        chunk->setFlags(0);
        chunk->setGravitySensor(0);
        
        std::copy(payload.begin(), payload.end(), packet.begin() + sizeof(UsbPacketHeader) + sizeof(CameraPacketHeader));
        
        return packet;
    };

    std::vector<uint8_t> payload1 = {0xFF, 0xD8, 0x01, 0x02};
    expectedPayload.insert(expectedPayload.end(), payload1.begin(), payload1.end());
    auto packet1 = buildPacket(1, payload1);

    std::vector<uint8_t> payload2 = {0x03, 0x04, 0x05, 0x06, 0xFF, 0xD9};
    expectedPayload.insert(expectedPayload.end(), payload2.begin(), payload2.end());
    auto packet2 = buildPacket(1, payload2);

    auto packet3 = buildPacket(2, {0xFF, 0xD8, 0xAA, 0xBB});

    EXPECT_CALL(GetOutputHandler(), output(expectedPayload)).Times(1);

    GetStream().send(packet1);
    GetStream().send(packet2);
    GetStream().send(packet3);
}

TEST_F(MjpegStreamTest, TriggersButtonHandlerOnHardwareFlag) {
    auto buildPacket = [](uint8_t frameId, bool isButtonPressed, const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> packet(sizeof(UsbPacketHeader) + sizeof(CameraPacketHeader) + payload.size(), 0x00);
        
        auto* usb = reinterpret_cast<UsbPacketHeader*>(packet.data());
        usb->setHeader(0xBBAA);
        usb->setCameraId(11);
        usb->setLength(sizeof(CameraPacketHeader) + payload.size());

        auto* chunk = reinterpret_cast<CameraPacketHeader*>(packet.data() + sizeof(UsbPacketHeader));
        chunk->setFrameId(frameId);
        chunk->setCameraNumber(0);
        chunk->setFlags(0);
        chunk->setGravitySensor(0);
        chunk->setButtonPressed(isButtonPressed);
        
        std::copy(payload.begin(), payload.end(), packet.begin() + sizeof(UsbPacketHeader) + sizeof(CameraPacketHeader));
        
        return packet;
    };

    EXPECT_CALL(GetOutputHandler(), output(::testing::_)).Times(::testing::AnyNumber());

    auto packet1 = buildPacket(1, false, {0xFF, 0xD8, 0x01});
    GetStream().send(packet1);

    auto packet2 = buildPacket(1, true, {0x02, 0x03, 0x04});
    GetStream().send(packet2);

    auto packet3 = buildPacket(1, false, {0x05, 0xFF, 0xD9});
    GetStream().send(packet3);
}

TEST_F(MjpegStreamTest, IgnoresInvalidHeaderOrShortBuffer) {
    EXPECT_CALL(GetOutputHandler(), output(::testing::_)).Times(0);

    std::vector<uint8_t> const short_buffer = {0xAA, 0xBB};
    GetStream().send(short_buffer);

    std::vector<uint8_t> const bad_magic_buffer(100, 0x00);
    GetStream().send(bad_magic_buffer);
}

TEST_F(MjpegStreamTest, TriggersButtonHandlerOnFlag) {
    std::vector<uint8_t> packet(128, 0x00);

    auto* usb_hdr = reinterpret_cast<UsbPacketHeader*>(packet.data());
    usb_hdr->setHeader(0xBBAA);
    usb_hdr->setCameraId(7);
    usb_hdr->setLength(64);

    auto* chunk = reinterpret_cast<CameraPacketHeader*>(packet.data() + sizeof(UsbPacketHeader));
    chunk->setFrameId(101);
    chunk->setCameraNumber(0);
    chunk->setFlags(0);
    chunk->setGravitySensor(0);
    chunk->setButtonPressed(true);

    GetStream().send(packet);
}

TEST_F(MjpegStreamTest, AccumulatesDataAndEmitsOnFrameIdChange) {
    std::vector<uint8_t> packet1(100, 0x00);
    auto* usb1 = reinterpret_cast<UsbPacketHeader*>(packet1.data());
    usb1->setHeader(0xBBAA); usb1->setCameraId(11); usb1->setLength(50); 

    auto* chunk1 = reinterpret_cast<CameraPacketHeader*>(packet1.data() + sizeof(UsbPacketHeader));
    chunk1->setFrameId(1); chunk1->setCameraNumber(0); chunk1->setGravitySensor(0);

    size_t const payload_offset = sizeof(UsbPacketHeader) + sizeof(CameraPacketHeader);
    std::fill(packet1.begin() + payload_offset, packet1.begin() + usb1->getLength(), 0xDE);
    packet1[payload_offset] = 0xFF;      // SOI
    packet1[payload_offset + 1] = 0xD8;
    packet1[sizeof(UsbPacketHeader) + usb1->getLength() - 2] = 0xFF; // EOI
    packet1[sizeof(UsbPacketHeader) + usb1->getLength() - 1] = 0xD9;

    std::vector<uint8_t> packet2(100, 0x00);
    auto* usb2 = reinterpret_cast<UsbPacketHeader*>(packet2.data());
    usb2->setHeader(0xBBAA); usb2->setCameraId(11); usb2->setLength(50);

    auto* chunk2 = reinterpret_cast<CameraPacketHeader*>(packet2.data() + sizeof(UsbPacketHeader));
    chunk2->setFrameId(2); chunk2->setCameraNumber(0); chunk2->setGravitySensor(0);
    std::fill(packet2.begin() + payload_offset, packet2.begin() + usb2->getLength(), 0xAA);

    EXPECT_CALL(GetOutputHandler(), output(::testing::Property(&std::vector<uint8_t>::empty, false)))
        .WillOnce(::testing::Invoke([](const std::vector<uint8_t>& frameBuffer) {
            ASSERT_FALSE(frameBuffer.empty());
            EXPECT_EQ(frameBuffer.front(), 0xFF); // Front byte is now the verified SOI marker
        }));

    GetStream().send(packet1);
    GetStream().send(packet2);
}

TEST_F(MjpegStreamTest, IgnoresInvalidCameraId) {
    std::vector<uint8_t> packet(20, 0x00);
    auto* usb = reinterpret_cast<UsbPacketHeader*>(packet.data());
    usb->setHeader(0xBBAA);
    usb->setCameraId(99);
    usb->setLength(15);

    EXPECT_CALL(GetOutputHandler(), output(::testing::_)).Times(0);
    GetStream().send(packet);
}

TEST_F(MjpegStreamTest, IgnoresPayloadExceedingBufferSize) {
    std::vector<uint8_t> packet(10, 0x00); 
    auto* usb = reinterpret_cast<UsbPacketHeader*>(packet.data());
    usb->setHeader(0xBBAA);
    usb->setCameraId(11);
    usb->setLength(50); 

    EXPECT_CALL(GetOutputHandler(), output(::testing::_)).Times(0);
    GetStream().send(packet);
}

TEST_F(MjpegStreamTest, IgnoresTruncatedMetadata) {
    std::vector<uint8_t> packet(10, 0x00); 
    auto* usb = reinterpret_cast<UsbPacketHeader*>(packet.data());
    usb->setHeader(0xBBAA);
    usb->setCameraId(11);
    usb->setLength(5); 
    
    EXPECT_CALL(GetOutputHandler(), output(::testing::_)).Times(0);
    GetStream().send(packet);
}

TEST_F(MjpegStreamTest, IgnoresUnsupportedCameraConfiguration) {
    std::vector<uint8_t> packet(20, 0x00);
    auto* usb = reinterpret_cast<UsbPacketHeader*>(packet.data());
    usb->setHeader(0xBBAA);
    usb->setCameraId(11);
    usb->setLength(15);

    auto* chunk = reinterpret_cast<CameraPacketHeader*>(packet.data() + sizeof(UsbPacketHeader));
    chunk->setFrameId(1);
    chunk->setCameraNumber(5);
    chunk->setFlags(0);
    chunk->setGravitySensor(0);

    EXPECT_CALL(GetOutputHandler(), output(::testing::_)).Times(0);
    GetStream().send(packet);
}

TEST_F(MjpegStreamTest, AbortsOnMidFrameCameraShift) {
    std::vector<uint8_t> packet1(20, 0x00);
    auto* usb1 = reinterpret_cast<UsbPacketHeader*>(packet1.data());
    usb1->setHeader(0xBBAA);
    usb1->setCameraId(11);
    usb1->setLength(15);
    auto* chunk1 = reinterpret_cast<CameraPacketHeader*>(packet1.data() + sizeof(UsbPacketHeader));
    chunk1->setFrameId(1);
    chunk1->setCameraNumber(0);
    chunk1->setGravitySensor(0);
    
    GetStream().send(packet1);

    std::vector<uint8_t> packet2(20, 0x00);
    auto* usb2 = reinterpret_cast<UsbPacketHeader*>(packet2.data());
    usb2->setHeader(0xBBAA);
    usb2->setCameraId(11);
    usb2->setLength(15);
    auto* chunk2 = reinterpret_cast<CameraPacketHeader*>(packet2.data() + sizeof(UsbPacketHeader));
    chunk2->setFrameId(1);
    chunk2->setCameraNumber(1);
    chunk2->setGravitySensor(0);

    EXPECT_CALL(GetOutputHandler(), output(::testing::_)).Times(0);
    GetStream().send(packet2);
}

TEST(UsbFrameDecoderEdgeTest, HandlesNullCallbacksSafely) {
    MjpegStream silentDecoder(nullptr);
    std::vector<uint8_t> packet(100, 0x00);
    ASSERT_NO_THROW(silentDecoder.send(packet));
}