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
#include "constants.hpp"
#include "data_structures.hpp"
#include "mjpeg_stream.hpp"

class MockHandlers {
public:
    MOCK_METHOD(void, output, (const std::span<const uint8_t> output));
};

class MjpegStreamTest : public ::testing::Test {
private:
    MockHandlers handler_;
    std::unique_ptr<MjpegStream> stream_;

protected:
    void SetUp() override {
        stream_ = std::make_unique<MjpegStream>(
            [this](std::span<const uint8_t> data) { 
                handler_.output(data); 
            }
        );
    }

    USB::PacketHeader* getPacketHeader(std::vector<uint8_t>& buffer) {
        return reinterpret_cast<USB::PacketHeader*>(buffer.data());
    }

    USB::PayloadHeader* getPayloadHeader(std::vector<uint8_t>& buffer) {
        return reinterpret_cast<USB::PayloadHeader*>(
            buffer.data() + USB::PacketHeaderSize
        );
    }

    MockHandlers& GetOutputHandler() { return handler_; }
    MjpegStream& GetStream() { return *stream_; }
};

TEST_F(MjpegStreamTest, ExtractsPhysicalBufferIgnoringDeclaredLength) {
    std::vector<uint8_t> packet(1024, 0xDD); 
    
    auto* packetHeader = getPacketHeader(packet);

    packetHeader->setHeader(UsbProtocol::USB_FRAME_HEADER);
    packetHeader->setCameraId(UsbProtocol::VIDEO_CAMERA_ID);
    packetHeader->setLength( 939);

    auto* payloadHeader = getPayloadHeader(packet);

    payloadHeader->setFrameId(2); 
    payloadHeader->setCameraNumber(0); 
    payloadHeader->setFlags(0); 
    payloadHeader->setGravitySensor(0);    

    packet[USB::TotalHeaderSize] = UsbProtocol::BOUNDARY_MARKER;
    packet[USB::TotalHeaderSize + 1] = UsbProtocol::START_MARKER;
    packet[USB::PacketHeaderSize + packetHeader->getLength() - 2] = UsbProtocol::BOUNDARY_MARKER;
    packet[USB::PacketHeaderSize + packetHeader->getLength() - 1] = UsbProtocol::END_MARKER;

    std::vector<uint8_t> triggerPacket = packet; 

    auto* triggerPayloadHeader = getPayloadHeader(triggerPacket);

    triggerPayloadHeader->setFrameId(3);

    std::vector<uint8_t> expectedOutput(
        packet.begin() + USB::PacketHeaderSize + USB::PayloadHeaderSize,
        packet.begin() + USB::PacketHeaderSize + packetHeader->getLength()
    );
    EXPECT_CALL(GetOutputHandler(), output(::testing::ElementsAreArray(expectedOutput))).Times(1);

    GetStream().send(packet);
    GetStream().send(triggerPacket);
}

TEST_F(MjpegStreamTest, SafelyIgnoresHardwareTailChunks) {
    std::vector<uint8_t> packet(1024, 0x00);

    auto* packetHeader = getPacketHeader(packet);
 
    packetHeader->setHeader(UsbProtocol::USB_FRAME_HEADER);
    packetHeader->setCameraId(UsbProtocol::VIDEO_CAMERA_ID);
    packetHeader->setLength(1024 - USB::PacketHeaderSize);

    auto* payloadHeader = getPayloadHeader(packet);

    payloadHeader->setFrameId(1);
    payloadHeader->setCameraNumber(0);
    payloadHeader->setFlags(0); 
    payloadHeader->setGravitySensor(0);    

    packet[USB::TotalHeaderSize] = UsbProtocol::BOUNDARY_MARKER;
    packet[USB::TotalHeaderSize + 1] = UsbProtocol::START_MARKER;
    packet[packet.size() - 2] = UsbProtocol::BOUNDARY_MARKER;
    packet[packet.size() - 1] = UsbProtocol::END_MARKER;

    std::vector<uint8_t> shortPacket(80, UsbProtocol::BOUNDARY_MARKER);
    std::vector<uint8_t> triggerPacket = packet;

    auto* triggerPayloadHeader = getPayloadHeader(triggerPacket);

    triggerPayloadHeader->setFrameId(2);   

    std::vector<uint8_t> expectedOutput(
        packet.begin() + USB::TotalHeaderSize,
        packet.end()
    );
    EXPECT_CALL(GetOutputHandler(), output(::testing::ElementsAreArray(expectedOutput))).Times(1);

    GetStream().send(packet);
    GetStream().send(shortPacket);
    GetStream().send(triggerPacket);
}

TEST_F(MjpegStreamTest, ReassemblesMultiChunkMjpegStream) {
    auto buildPacket = [this](uint8_t frameId, const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> packet(
            USB::PacketHeaderSize + USB::PayloadHeaderSize + payload.size(), 
            0x00
        );
        
        USB::PacketHeader* packetHeader = getPacketHeader(packet);

        packetHeader->setHeader(UsbProtocol::USB_FRAME_HEADER);
        packetHeader->setCameraId(UsbProtocol::VIDEO_CAMERA_ID);
        packetHeader->setLength(USB::PayloadHeaderSize + payload.size());

        auto* payloadHeader = getPayloadHeader(packet);

        payloadHeader->setFrameId(frameId);
        payloadHeader->setCameraNumber(0);
        payloadHeader->setFlags(0);
        payloadHeader->setGravitySensor(0);
        
        std::copy(
            payload.begin(), 
            payload.end(), 
            packet.begin() + USB::PacketHeaderSize + USB::PayloadHeaderSize);
        
        return packet;
    };

    std::vector<uint8_t> expectedOutput;
    std::vector<uint8_t> payload1 = {
        UsbProtocol::BOUNDARY_MARKER, 
        UsbProtocol::START_MARKER, 
        0x01, 
        0x02
    };

    expectedOutput.insert(
        expectedOutput.end(), 
        payload1.begin(), 
        payload1.end()
    );
    
    std::vector<uint8_t> packet1buffer = buildPacket(
        1, 
        payload1
    );

    std::vector<uint8_t> payload2 = {
        0x03, 
        0x04, 
        0x05, 
        0x06, 
        UsbProtocol::BOUNDARY_MARKER, 
        UsbProtocol::END_MARKER
    };

    expectedOutput.insert(
        expectedOutput.end(), 
        payload2.begin(), 
        payload2.end()
    );
    
    std::vector<uint8_t> packet2 = buildPacket(
        1, 
        payload2
    );

    std::vector<uint8_t> packet3 = buildPacket(2, {
        UsbProtocol::BOUNDARY_MARKER, 
        UsbProtocol::START_MARKER, 
        UsbProtocol::USB_FRAME_HEADER_A, 
        UsbProtocol::USB_FRAME_HEADER_B
    });

    EXPECT_CALL(GetOutputHandler(), output(::testing::ElementsAreArray(expectedOutput))).Times(1);

    GetStream().send(packet1buffer);
    GetStream().send(packet2);
    GetStream().send(packet3);
}

TEST_F(MjpegStreamTest, IgnoresInvalidHeaderOrShortBuffer) {
    EXPECT_CALL(GetOutputHandler(), output(::testing::_)).Times(0);

    std::vector<uint8_t> const shortPacket = {
        UsbProtocol::USB_FRAME_HEADER_A, 
        UsbProtocol::USB_FRAME_HEADER_B
    };
    GetStream().send(shortPacket);

    std::vector<uint8_t> const emptyPacket(100, 0x00);
    GetStream().send(emptyPacket);
}

TEST_F(MjpegStreamTest, AccumulatesDataAndEmitsOnFrameIdChange) {
    std::vector<uint8_t> packet1(100, 0x00);

    auto* packetHeader1 = getPacketHeader(packet1);

    packetHeader1->setHeader(UsbProtocol::USB_FRAME_HEADER);
    packetHeader1->setCameraId(UsbProtocol::VIDEO_CAMERA_ID);
    packetHeader1->setLength(50); 

    auto* payloadHeader1 = getPayloadHeader(packet1);

    payloadHeader1->setFrameId(1);
    payloadHeader1->setCameraNumber(0);
    payloadHeader1->setGravitySensor(0);;

    std::fill(
        packet1.begin() + USB::TotalHeaderSize, 
        packet1.begin() + packetHeader1->getLength(), 
        0xDE
    );

    packet1[USB::TotalHeaderSize] = UsbProtocol::BOUNDARY_MARKER;
    packet1[USB::TotalHeaderSize + 1] = UsbProtocol::START_MARKER;

    packet1[USB::PacketHeaderSize + packetHeader1->getLength() - 2] = UsbProtocol::BOUNDARY_MARKER;
    packet1[USB::PacketHeaderSize + packetHeader1->getLength() - 1] = UsbProtocol::END_MARKER;

    std::vector<uint8_t> packet2(100, 0x00);

    auto* packetHeader2 = getPacketHeader(packet2);

    packetHeader2->setHeader(UsbProtocol::USB_FRAME_HEADER);
    packetHeader2->setCameraId(UsbProtocol::VIDEO_CAMERA_ID);
    packetHeader2->setLength(50);

    auto* payloadHeader2 = getPayloadHeader(packet2);

    payloadHeader2->setFrameId(2);
    payloadHeader2->setCameraNumber(0);
    payloadHeader2->setGravitySensor(0);

    std::fill(
        packet2.begin() + USB::TotalHeaderSize, 
        packet2.begin() + packetHeader2->getLength(), 
        UsbProtocol::USB_FRAME_HEADER_A
    );

    EXPECT_CALL(
        GetOutputHandler(), 
        output(::testing::Not(::testing::IsEmpty()))
    )
    .WillOnce(
        ::testing::Invoke([](std::span<const uint8_t> frameBuffer) {
            ASSERT_FALSE(frameBuffer.empty());
            EXPECT_EQ(frameBuffer.front(), UsbProtocol::BOUNDARY_MARKER);
        }
    ));

    GetStream().send(packet1);
    GetStream().send(packet2);
}

TEST_F(MjpegStreamTest, IgnoresInvalidCameraId) {
    std::vector<uint8_t> packet(20, 0x00);

    auto* packetHeader = getPacketHeader(packet);

    packetHeader->setHeader(UsbProtocol::USB_FRAME_HEADER);
    packetHeader->setCameraId(99);
    packetHeader->setLength(15);

    EXPECT_CALL(GetOutputHandler(), output(::testing::_)).Times(0);
    GetStream().send(packet);
}

TEST_F(MjpegStreamTest, IgnoresPayloadExceedingBufferSize) {
    std::vector<uint8_t> packet(10, 0x00); 

    auto* packetHeader = getPacketHeader(packet);

    packetHeader->setHeader(UsbProtocol::USB_FRAME_HEADER);
    packetHeader->setCameraId(UsbProtocol::VIDEO_CAMERA_ID);
    packetHeader->setLength(50); 

    EXPECT_CALL(GetOutputHandler(), output(::testing::_)).Times(0);
    GetStream().send(packet);
}

TEST_F(MjpegStreamTest, IgnoresTruncatedMetadata) {
    std::vector<uint8_t> packet(10, 0x00); 

    auto* packetHeader = getPacketHeader(packet);

    packetHeader->setHeader(UsbProtocol::USB_FRAME_HEADER);
    packetHeader->setCameraId(UsbProtocol::VIDEO_CAMERA_ID);
    packetHeader->setLength(5); 
    
    EXPECT_CALL(GetOutputHandler(), output(::testing::_)).Times(0);
    GetStream().send(packet);
}

TEST_F(MjpegStreamTest, IgnoresUnsupportedCameraConfiguration) {
    std::vector<uint8_t> packet(20, 0x00);

    auto* packetHeader = getPacketHeader(packet);

    packetHeader->setHeader(UsbProtocol::USB_FRAME_HEADER);
    packetHeader->setCameraId(UsbProtocol::VIDEO_CAMERA_ID);
    packetHeader->setLength(15);

    auto* payloadHeader = getPayloadHeader(packet);

    payloadHeader->setFrameId(1);
    payloadHeader->setCameraNumber(5);
    payloadHeader->setFlags(0);
    payloadHeader->setGravitySensor(0);

    EXPECT_CALL(GetOutputHandler(), output(::testing::_)).Times(0);
    GetStream().send(packet);
}

TEST_F(MjpegStreamTest, AbortsOnMidFrameCameraShift) {
    std::vector<uint8_t> packet1(20, 0x00);
    auto* packetHeader1 = getPacketHeader(packet1);

    packetHeader1->setHeader(UsbProtocol::USB_FRAME_HEADER);
    packetHeader1->setCameraId(UsbProtocol::VIDEO_CAMERA_ID);
    packetHeader1->setLength(15);

    auto* payloadHeader1 = getPayloadHeader(packet1);

    payloadHeader1->setFrameId(1);
    payloadHeader1->setCameraNumber(0);
    payloadHeader1->setGravitySensor(0);
    
    GetStream().send(packet1);

    std::vector<uint8_t> packet2(20, 0x00);

    auto* packetHeader2 = getPacketHeader(packet2);
    
    packetHeader2->setHeader(UsbProtocol::USB_FRAME_HEADER);
    packetHeader2->setCameraId(UsbProtocol::VIDEO_CAMERA_ID);
    packetHeader2->setLength(15);
    
    auto* payloadHeader2 = getPayloadHeader(packet2);

    payloadHeader2->setFrameId(1);
    payloadHeader2->setCameraNumber(1);
    payloadHeader2->setGravitySensor(0);

    EXPECT_CALL(GetOutputHandler(), output(::testing::_)).Times(0);
    GetStream().send(packet2);
}

TEST(UsbFrameDecoderEdgeTest, HandlesNullCallbacksSafely) {
    MjpegStream silentDecoder(nullptr);
    std::vector<uint8_t> packet(100, 0x00);
    ASSERT_NO_THROW(silentDecoder.send(packet));
}

TEST_F(MjpegStreamTest, PreventsIntegerUnderflowOnUndersizedHardwareLength) {
    std::vector<uint8_t> malformedPacket(USB::TotalHeaderSize, 0x00);
    
    auto* packetHeader = getPacketHeader(malformedPacket);
    packetHeader->setHeader(UsbProtocol::USB_FRAME_HEADER);
    packetHeader->setCameraId(UsbProtocol::VIDEO_CAMERA_ID);

    packetHeader->setLength(2);

    EXPECT_CALL(GetOutputHandler(), output(::testing::_)).Times(0);

    ASSERT_NO_THROW({
        GetStream().send(malformedPacket);
    });
}