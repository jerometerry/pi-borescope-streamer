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

inline constexpr size_t uPktSz = USB::uPktSz;
inline constexpr size_t cPktSz = USB::cPktSz;
inline constexpr size_t tPktSz = USB::tPktSz;
inline constexpr uint16_t PACKET_HEADER = USB::PACKET_HEADER;
inline constexpr uint8_t HEADER_A = USB::HEADER_A;
inline constexpr uint8_t HEADER_B = USB::HEADER_B;
inline constexpr uint8_t BOUNDARY_MARKER = USB::BOUNDARY_MARKER;
inline constexpr uint8_t START_MARKER = USB::START_MARKER;
inline constexpr uint8_t END_MARKER = USB::END_MARKER;
inline constexpr uint8_t CAM_11 = UsbProtocol::VIDEO_CAMERA_ID;
inline constexpr uint8_t CAM_7 = UsbProtocol::GRAVITY_SENSOR_CAMERA_ID;

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
            [this](const std::vector<uint8_t>& data) { 
                handler_.output(data); 
            }
        );
    }

    MockHandlers& GetOutputHandler() { return handler_; }
    MjpegStream& GetStream() { return *stream_; }
};

TEST_F(MjpegStreamTest, ExtractsPhysicalBufferIgnoringDeclaredLength) {
    std::vector<uint8_t> hardwarePacket(1024, 0xDD); 
    
    auto* usb = 
        reinterpret_cast<USB::UsbPacketHeader*>(
            hardwarePacket.data()
        );

    usb->setHeader(PACKET_HEADER);
    usb->setCameraId(CAM_11);
    usb->setLength( 939);

    auto* chunk = 
        reinterpret_cast<USB::CameraPacketHeader*>(
            hardwarePacket.data() + uPktSz
        );

    chunk->setFrameId(2); 
    chunk->setCameraNumber(0); 
    chunk->setFlags(0); 
    chunk->setGravitySensor(0);    

    hardwarePacket[tPktSz] = BOUNDARY_MARKER;
    hardwarePacket[tPktSz + 1] = START_MARKER;
    hardwarePacket[uPktSz + usb->getLength() - 2] = BOUNDARY_MARKER;
    hardwarePacket[uPktSz + usb->getLength() - 1] = END_MARKER;

    std::vector<uint8_t> triggerFrame = hardwarePacket; 

    auto* triggerChunk = 
        reinterpret_cast<USB::CameraPacketHeader*>(
            triggerFrame.data() + uPktSz
        );

    triggerChunk->setFrameId(3);

    std::vector<uint8_t> expectedPayload(
        hardwarePacket.begin() + uPktSz + cPktSz,
        hardwarePacket.begin() + uPktSz + usb->getLength()
    );
    EXPECT_CALL(GetOutputHandler(), output(expectedPayload)).Times(1);

    GetStream().send(hardwarePacket);
    GetStream().send(triggerFrame);
}

TEST_F(MjpegStreamTest, SafelyIgnoresHardwareTailChunks) {
    std::vector<uint8_t> validHeader(1024, 0x00);

    auto* usb = 
        reinterpret_cast<USB::UsbPacketHeader*>(
            validHeader.data()
    );
 
    usb->setHeader(PACKET_HEADER);
    usb->setCameraId(CAM_11);
    usb->setLength(1024 - uPktSz);

    auto* chunk = 
        reinterpret_cast<USB::CameraPacketHeader*>(
            validHeader.data() + uPktSz
        );

    chunk->setFrameId(1);
    chunk->setCameraNumber(0);
    chunk->setFlags(0); 
    chunk->setGravitySensor(0);    

    validHeader[tPktSz] = BOUNDARY_MARKER;
    validHeader[tPktSz + 1] = START_MARKER;
    validHeader[validHeader.size() - 2] = BOUNDARY_MARKER;
    validHeader[validHeader.size() - 1] = END_MARKER;

    std::vector<uint8_t> shortPacketTail(80, BOUNDARY_MARKER);
    std::vector<uint8_t> triggerFrame = validHeader;

    auto* triggerChunk = 
        reinterpret_cast<USB::CameraPacketHeader*>(
            triggerFrame.data() + uPktSz
        );

    triggerChunk->setFrameId(2);   

    std::vector<uint8_t> expectedPayload(
        validHeader.begin() + tPktSz,
        validHeader.end()
    );
    EXPECT_CALL(GetOutputHandler(), output(expectedPayload)).Times(1);

    GetStream().send(validHeader);
    GetStream().send(shortPacketTail);
    GetStream().send(triggerFrame);
}


TEST_F(MjpegStreamTest, ReassemblesMultiChunkMjpegStream) {
    std::vector<uint8_t> expectedPayload;

    auto buildPacket = [](
            uint8_t frameId, 
            const std::vector<uint8_t>& payload) {

        std::vector<uint8_t> packet(
            uPktSz + cPktSz + payload.size(), 
            0x00
        );
        
        auto* usb = 
            reinterpret_cast<USB::UsbPacketHeader*>(
                packet.data()
            );

        usb->setHeader(PACKET_HEADER);
        usb->setCameraId(CAM_11);
        usb->setLength(cPktSz + payload.size());

        auto* chunk = 
            reinterpret_cast<USB::CameraPacketHeader*>(
                packet.data() + uPktSz
            );

        chunk->setFrameId(frameId);
        chunk->setCameraNumber(0);
        chunk->setFlags(0);
        chunk->setGravitySensor(0);
        
        std::copy(
            payload.begin(), 
            payload.end(), 
            packet.begin() + uPktSz + cPktSz);
        
        return packet;
    };

    std::vector<uint8_t> payload1 = {BOUNDARY_MARKER, START_MARKER, 0x01, 0x02};

    expectedPayload.insert(
        expectedPayload.end(), 
        payload1.begin(), 
        payload1.end()
    );
    
    auto packet1 = buildPacket(
        1, 
        payload1
    );

    std::vector<uint8_t> payload2 = {0x03, 0x04, 0x05, 0x06, BOUNDARY_MARKER, END_MARKER};

    expectedPayload.insert(
        expectedPayload.end(), 
        payload2.begin(), 
        payload2.end()
    );
    
    auto packet2 = buildPacket(
        1, 
        payload2
    );

    auto packet3 = buildPacket(
        2, 
        {BOUNDARY_MARKER, START_MARKER, HEADER_A, HEADER_B}
    );

    EXPECT_CALL(GetOutputHandler(), output(expectedPayload)).Times(1);

    GetStream().send(packet1);
    GetStream().send(packet2);
    GetStream().send(packet3);
}

TEST_F(MjpegStreamTest, TriggersButtonHandlerOnHardwareFlag) {
    auto buildPacket = [](
            uint8_t frameId, 
            bool isButtonPressed, 
            const std::vector<uint8_t>& payload) {

        std::vector<uint8_t> packet(
            uPktSz + cPktSz + payload.size(), 
            0x00
        );
        
        auto* usb = 
            reinterpret_cast<USB::UsbPacketHeader*>(
                packet.data()
            );

        usb->setHeader(PACKET_HEADER);
        usb->setCameraId(CAM_11);
        usb->setLength(cPktSz + payload.size());

        auto* chunk = 
            reinterpret_cast<USB::CameraPacketHeader*>(
                packet.data() + uPktSz
            );

        chunk->setFrameId(frameId);
        chunk->setCameraNumber(0);
        chunk->setFlags(0);
        chunk->setGravitySensor(0);
        chunk->setButtonPressed(isButtonPressed);
        
        std::copy(
            payload.begin(), 
            payload.end(), 
            packet.begin() + uPktSz + cPktSz
        );
        
        return packet;
    };

    EXPECT_CALL(
        GetOutputHandler(), 
        output(::testing::_)
    )
    .Times(
        ::testing::AnyNumber()
    );

    auto packet1 = buildPacket(
        1, 
        false, 
        {BOUNDARY_MARKER, START_MARKER, 0x01}
    );
    GetStream().send(packet1);

    auto packet2 = buildPacket(
        1, 
        true, 
        {0x02, 0x03, 0x04}
    );
    GetStream().send(packet2);

    auto packet3 = buildPacket(
        1, 
        false, 
        {0x05, BOUNDARY_MARKER, END_MARKER}
    );
    GetStream().send(packet3);
}

TEST_F(MjpegStreamTest, IgnoresInvalidHeaderOrShortBuffer) {
    EXPECT_CALL(GetOutputHandler(), output(::testing::_)).Times(0);

    std::vector<uint8_t> const short_buffer = {HEADER_A, HEADER_B};
    GetStream().send(short_buffer);

    std::vector<uint8_t> const bad_magic_buffer(100, 0x00);
    GetStream().send(bad_magic_buffer);
}

TEST_F(MjpegStreamTest, TriggersButtonHandlerOnFlag) {
    std::vector<uint8_t> packet(128, 0x00);

    auto* usb_hdr = 
        reinterpret_cast<USB::UsbPacketHeader*>(
            packet.data()
        );

    usb_hdr->setHeader(PACKET_HEADER);
    usb_hdr->setCameraId(CAM_7);
    usb_hdr->setLength(64);

    auto* chunk = 
        reinterpret_cast<USB::CameraPacketHeader*>(packet.data() + 
            uPktSz);

    chunk->setFrameId(101);
    chunk->setCameraNumber(0);
    chunk->setFlags(0);
    chunk->setGravitySensor(0);
    chunk->setButtonPressed(true);

    GetStream().send(packet);
}

TEST_F(MjpegStreamTest, AccumulatesDataAndEmitsOnFrameIdChange) {
    std::vector<uint8_t> packet1(100, 0x00);

    auto* usb1 = 
        reinterpret_cast<USB::UsbPacketHeader*>(packet1.data());

    usb1->setHeader(PACKET_HEADER);
    usb1->setCameraId(CAM_11);
    usb1->setLength(50); 

    auto* chunk1 = 
        reinterpret_cast<USB::CameraPacketHeader*>(
            packet1.data() + uPktSz
        );

    chunk1->setFrameId(1);
    chunk1->setCameraNumber(0);
    chunk1->setGravitySensor(0);;

    std::fill(
        packet1.begin() + tPktSz, 
        packet1.begin() + usb1->getLength(), 
        0xDE
    );

    packet1[tPktSz] = BOUNDARY_MARKER;
    packet1[tPktSz + 1] = START_MARKER;

    packet1[uPktSz + usb1->getLength() - 2] = BOUNDARY_MARKER;
    packet1[uPktSz + usb1->getLength() - 1] = END_MARKER;

    std::vector<uint8_t> packet2(100, 0x00);

    auto* usb2 = 
        reinterpret_cast<USB::UsbPacketHeader*>(
            packet2.data()
        );

    usb2->setHeader(PACKET_HEADER);
    usb2->setCameraId(CAM_11);
    usb2->setLength(50);

    auto* chunk2 = 
        reinterpret_cast<USB::CameraPacketHeader*>(
            packet2.data() + uPktSz
        );

    chunk2->setFrameId(2);
    chunk2->setCameraNumber(0);
    chunk2->setGravitySensor(0);

    std::fill(
        packet2.begin() + tPktSz, 
        packet2.begin() + usb2->getLength(), 
        HEADER_A
    );

    EXPECT_CALL(
        GetOutputHandler(), 
        output(
            ::testing::Property(
                &std::vector<uint8_t>::empty, 
                false
            )
        )
    )
    .WillOnce(
        ::testing::Invoke([](
            const std::vector<uint8_t>& frameBuffer
        ) {
            ASSERT_FALSE(frameBuffer.empty());
            EXPECT_EQ(frameBuffer.front(), BOUNDARY_MARKER);
        }
    ));

    GetStream().send(packet1);
    GetStream().send(packet2);
}

TEST_F(MjpegStreamTest, IgnoresInvalidCameraId) {
    std::vector<uint8_t> packet(20, 0x00);

    auto* usb = 
        reinterpret_cast<USB::UsbPacketHeader*>(
            packet.data()
        );

    usb->setHeader(PACKET_HEADER);
    usb->setCameraId(99);
    usb->setLength(15);

    EXPECT_CALL(GetOutputHandler(), output(::testing::_)).Times(0);
    GetStream().send(packet);
}

TEST_F(MjpegStreamTest, IgnoresPayloadExceedingBufferSize) {
    std::vector<uint8_t> packet(10, 0x00); 

    auto* usb = 
        reinterpret_cast<USB::UsbPacketHeader*>(
            packet.data()
        );

    usb->setHeader(PACKET_HEADER);
    usb->setCameraId(CAM_11);
    usb->setLength(50); 

    EXPECT_CALL(GetOutputHandler(), output(::testing::_)).Times(0);
    GetStream().send(packet);
}

TEST_F(MjpegStreamTest, IgnoresTruncatedMetadata) {
    std::vector<uint8_t> packet(10, 0x00); 

    auto* usb = 
        reinterpret_cast<USB::UsbPacketHeader*>(
            packet.data()
        );

    usb->setHeader(PACKET_HEADER);
    usb->setCameraId(CAM_11);
    usb->setLength(5); 
    
    EXPECT_CALL(GetOutputHandler(), output(::testing::_)).Times(0);
    GetStream().send(packet);
}

TEST_F(MjpegStreamTest, IgnoresUnsupportedCameraConfiguration) {
    std::vector<uint8_t> packet(20, 0x00);

    auto* usb = 
        reinterpret_cast<USB::UsbPacketHeader*>(
            packet.data()
        );

    usb->setHeader(PACKET_HEADER);
    usb->setCameraId(CAM_11);
    usb->setLength(15);

    auto* chunk = 
        reinterpret_cast<USB::CameraPacketHeader*>(
            packet.data() + uPktSz
        );

    chunk->setFrameId(1);
    chunk->setCameraNumber(5);
    chunk->setFlags(0);
    chunk->setGravitySensor(0);

    EXPECT_CALL(GetOutputHandler(), output(::testing::_)).Times(0);
    GetStream().send(packet);
}

TEST_F(MjpegStreamTest, AbortsOnMidFrameCameraShift) {
    std::vector<uint8_t> packet1(20, 0x00);
    auto* usb1 = 
        reinterpret_cast<USB::UsbPacketHeader*>(packet1.data());

    usb1->setHeader(PACKET_HEADER);
    usb1->setCameraId(CAM_11);
    usb1->setLength(15);

    auto* chunk1 = 
        reinterpret_cast<USB::CameraPacketHeader*>(
            packet1.data() + uPktSz
        );

    chunk1->setFrameId(1);
    chunk1->setCameraNumber(0);
    chunk1->setGravitySensor(0);
    
    GetStream().send(packet1);

    std::vector<uint8_t> packet2(20, 0x00);

    auto* usb2 = 
        reinterpret_cast<USB::UsbPacketHeader*>(
            packet2.data()
        );
    
    usb2->setHeader(PACKET_HEADER);
    usb2->setCameraId(CAM_11);
    usb2->setLength(15);
    
    auto* chunk2 = 
        reinterpret_cast<USB::CameraPacketHeader*>(
            packet2.data() + uPktSz
        );

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