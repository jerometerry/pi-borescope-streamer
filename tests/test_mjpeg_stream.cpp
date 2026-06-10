#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <vector>
#include "constants.hpp"
#include "frame.hpp"
#include "frame_disruptor.hpp"
#include "mjpeg_stream.hpp"
#include "usb_packet_header.hpp"
#include "usb_payload_header.hpp"

class MjpegStreamTest : public ::testing::Test {
private:
    FrameDisruptor disruptor_;
    MjpegStream stream_;
    int64_t next_read_seq_{0};

public:
    MjpegStreamTest() 
        : disruptor_(), 
          stream_(disruptor_)
    {
        disruptor_.preAllocate(Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);
    }    

protected:
    static UsbPacketHeader* getPacketHeader(std::span<uint8_t> buffer) {
        return reinterpret_cast<UsbPacketHeader*>(buffer.data());
    }

    static UsbPayloadHeader* getPayloadHeader(std::span<uint8_t> buffer) {
        return reinterpret_cast<UsbPayloadHeader*>(buffer.data() + USB_PACKET_HEADER_SIZE);
    }

    MjpegStream& GetStream() { return stream_; }

    bool verifyNextPublishedFramed(std::vector<uint8_t>& out_frame_data) {
        int64_t available = disruptor_.getHighestPublished();
        
        while (next_read_seq_ <= available) {
            Frame& slot = disruptor_.getBySequence(next_read_seq_);
            
            if (slot.contentSize() > 0) {
                auto slice = slot.getContentSlice();
                out_frame_data.assign(slice.begin(), slice.end());
                next_read_seq_++;
                disruptor_.markConsumed(next_read_seq_ - 1);
                return true;
            }

            next_read_seq_++;
            disruptor_.markConsumed(next_read_seq_ - 1);
        }

        std::cerr << "Verify failed. No valid frames available up to seq: " << available << "\n";
        return false;
    }

    void verifyNoValidFramesPublished() {
        int64_t available = disruptor_.getHighestPublished();
        while (next_read_seq_ <= available) {
            Frame& slot = disruptor_.getBySequence(next_read_seq_);
            EXPECT_EQ(slot.contentSize(), 0) 
                << "Found unexpected valid frame at sequence " << next_read_seq_;
            
            next_read_seq_++;
            disruptor_.markConsumed(next_read_seq_ - 1);
        }
    }
};

// NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members)
MATCHER_P(FrameDataEq, expectedOutput, "BufferPtr internal data matches expected output") {
    if (!arg) {
        *result_listener << "which is a null BufferPtr";
        return false;
    }

    return ::testing::ExplainMatchResult(
        ::testing::ElementsAreArray(expectedOutput), 
        arg->getContentSlice(), 
        result_listener
    );
}

MATCHER_P(FrameStartsWith, expectedFront, "BufferPtr internal data starts with expected byte") {
    if (!arg) {
        *result_listener << "which is a null BufferPtr";
        return false;
    }

    if (arg->empty()) {
        *result_listener << "which points to an empty payload buffer";
        return false;
    }

    return ::testing::ExplainMatchResult(
        ::testing::Eq(expectedFront), 
        arg->front(), 
        result_listener
    );
}
// NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)

TEST_F(MjpegStreamTest, TestUsbPayloadHeaderGettersAndSetters) {
    std::vector<uint8_t> packet(1024, 0xDD); 
    
    auto* packetHeader = getPacketHeader(packet);

    packetHeader->setHeader(UsbProtocol::USB_FRAME_HEADER);
    packetHeader->setCameraId(UsbProtocol::VIDEO_CAMERA_ID);
    packetHeader->setLength( 939);

    auto* payloadHeader = getPayloadHeader(packet);

    payloadHeader->setFrameId(9); 
    payloadHeader->setCameraNumber(8); 
    payloadHeader->setGravitySensor(7);    
    payloadHeader->setHasGravitySensor(true);
    payloadHeader->setButtonPressed(true);
    payloadHeader->setOtherFlags(3);

    packet[TOTAL_USB_HEADER_SIZE] = UsbProtocol::BOUNDARY_MARKER;
    packet[TOTAL_USB_HEADER_SIZE + 1] = UsbProtocol::START_MARKER;
    packet[USB_PACKET_HEADER_SIZE + packetHeader->getLength() - 2] = UsbProtocol::BOUNDARY_MARKER;
    packet[USB_PACKET_HEADER_SIZE + packetHeader->getLength() - 1] = UsbProtocol::END_MARKER;


    EXPECT_EQ(payloadHeader->getFrameId(), 9);
    EXPECT_EQ(payloadHeader->getCameraNumber(), 8);
    EXPECT_EQ(payloadHeader->getGravitySensor(), 7);
    EXPECT_EQ(payloadHeader->hasGravitySensor(), true);
    EXPECT_EQ(payloadHeader->isButtonPressed(), true);
    EXPECT_EQ(payloadHeader->getOtherFlags(), 3);
}

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

    packet[TOTAL_USB_HEADER_SIZE] = UsbProtocol::BOUNDARY_MARKER;
    packet[TOTAL_USB_HEADER_SIZE + 1] = UsbProtocol::START_MARKER;
    packet[USB_PACKET_HEADER_SIZE + packetHeader->getLength() - 2] = UsbProtocol::BOUNDARY_MARKER;
    packet[USB_PACKET_HEADER_SIZE + packetHeader->getLength() - 1] = UsbProtocol::END_MARKER;

    std::vector<uint8_t> triggerPacket = packet; 

    auto* triggerPayloadHeader = getPayloadHeader(triggerPacket);

    triggerPayloadHeader->setFrameId(3);

    GetStream().send(packet);
    GetStream().send(triggerPacket);

    std::vector<uint8_t> actualOutputFrame;
    ASSERT_TRUE(verifyNextPublishedFramed(actualOutputFrame)) 
        << "Stream failed to publish completed frame block!";

    std::vector<uint8_t> expectedOutput(
        packet.begin() + TOTAL_USB_HEADER_SIZE,
        packet.begin() + USB_PACKET_HEADER_SIZE + packetHeader->getLength()
    );
    EXPECT_EQ(actualOutputFrame, expectedOutput);
}

TEST_F(MjpegStreamTest, SafelyIgnoresHardwareTailChunks) {
    std::vector<uint8_t> packet(1024, 0x00);

    auto* packetHeader = getPacketHeader(packet);
 
    packetHeader->setHeader(UsbProtocol::USB_FRAME_HEADER);
    packetHeader->setCameraId(UsbProtocol::VIDEO_CAMERA_ID);
    packetHeader->setLength(1024 - USB_PACKET_HEADER_SIZE);

    auto* payloadHeader = getPayloadHeader(packet);

    payloadHeader->setFrameId(1);
    payloadHeader->setCameraNumber(0);
    payloadHeader->setFlags(0); 
    payloadHeader->setGravitySensor(0);    

    packet[TOTAL_USB_HEADER_SIZE] = UsbProtocol::BOUNDARY_MARKER;
    packet[TOTAL_USB_HEADER_SIZE + 1] = UsbProtocol::START_MARKER;
    packet[packet.size() - 2] = UsbProtocol::BOUNDARY_MARKER;
    packet[packet.size() - 1] = UsbProtocol::END_MARKER;

    std::vector<uint8_t> shortPacket(80, UsbProtocol::BOUNDARY_MARKER);
    std::vector<uint8_t> triggerPacket = packet;

    auto* triggerPayloadHeader = getPayloadHeader(triggerPacket);

    triggerPayloadHeader->setFrameId(2);   

    GetStream().send(packet);
    GetStream().send(shortPacket);
    GetStream().send(triggerPacket);

    std::vector<uint8_t> actualOutputFrame;
    ASSERT_TRUE(verifyNextPublishedFramed(actualOutputFrame)) 
        << "Stream failed to publish completed frame block!";

    std::vector<uint8_t> expectedOutput(
        packet.begin() + TOTAL_USB_HEADER_SIZE,
        packet.end()
    );
    EXPECT_EQ(actualOutputFrame, expectedOutput);
}

TEST_F(MjpegStreamTest, ReassemblesMultiChunkMjpegStream) {
    auto buildPacket = [](uint8_t frameId, const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> packet(
            USB_PACKET_HEADER_SIZE + USB_PAYLOAD_HEADER_SIZE + payload.size(), 
            0x00
        );
        
        UsbPacketHeader* packetHeader = getPacketHeader(packet);

        packetHeader->setHeader(UsbProtocol::USB_FRAME_HEADER);
        packetHeader->setCameraId(UsbProtocol::VIDEO_CAMERA_ID);
        packetHeader->setLength(USB_PAYLOAD_HEADER_SIZE + payload.size());

        auto* payloadHeader = getPayloadHeader(packet);

        payloadHeader->setFrameId(frameId);
        payloadHeader->setCameraNumber(0);
        payloadHeader->setFlags(0);
        payloadHeader->setGravitySensor(0);
        
        std::copy(
            payload.begin(), 
            payload.end(), 
            packet.begin() + USB_PACKET_HEADER_SIZE + USB_PAYLOAD_HEADER_SIZE);
        
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

    GetStream().send(packet1buffer);
    GetStream().send(packet2);
    GetStream().send(packet3);

    std::vector<uint8_t> actualOutputFrame;
    ASSERT_TRUE(verifyNextPublishedFramed(actualOutputFrame)) 
        << "Stream failed to publish completed frame block!";

    EXPECT_EQ(actualOutputFrame, expectedOutput);
}

TEST_F(MjpegStreamTest, IgnoresInvalidHeaderOrShortBuffer) {
    std::vector<uint8_t> const shortPacket = {
        UsbProtocol::USB_FRAME_HEADER_A, 
        UsbProtocol::USB_FRAME_HEADER_B
    };
    GetStream().send(shortPacket);

    std::vector<uint8_t> const emptyPacket(100, 0x00);
    GetStream().send(emptyPacket);

    verifyNoValidFramesPublished();
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
        packet1.begin() + TOTAL_USB_HEADER_SIZE, 
        packet1.begin() + packetHeader1->getLength(), 
        0xDE
    );

    packet1[TOTAL_USB_HEADER_SIZE] = UsbProtocol::BOUNDARY_MARKER;
    packet1[TOTAL_USB_HEADER_SIZE + 1] = UsbProtocol::START_MARKER;

    packet1[USB_PACKET_HEADER_SIZE + packetHeader1->getLength() - 2] = UsbProtocol::BOUNDARY_MARKER;
    packet1[USB_PACKET_HEADER_SIZE + packetHeader1->getLength() - 1] = UsbProtocol::END_MARKER;

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
        packet2.begin() + TOTAL_USB_HEADER_SIZE, 
        packet2.begin() + packetHeader2->getLength(), 
        UsbProtocol::USB_FRAME_HEADER_A
    );

    GetStream().send(packet1);
    GetStream().send(packet2);

    std::vector<uint8_t> actualOutputFrame;
    ASSERT_TRUE(verifyNextPublishedFramed(actualOutputFrame)) 
        << "Stream failed to publish completed frame block!";

    EXPECT_EQ(actualOutputFrame.front(), UsbProtocol::BOUNDARY_MARKER);
}

TEST_F(MjpegStreamTest, IgnoresInvalidCameraId) {
    std::vector<uint8_t> packet(20, 0x00);
    auto* packetHeader = getPacketHeader(packet);
    packetHeader->setHeader(UsbProtocol::USB_FRAME_HEADER);
    packetHeader->setCameraId(99);
    packetHeader->setLength(15);

    GetStream().send(packet);
    verifyNoValidFramesPublished();
}

TEST_F(MjpegStreamTest, IgnoresPayloadExceedingBufferSize) {
    std::vector<uint8_t> packet(10, 0x00); 
    auto* packetHeader = getPacketHeader(packet);
    packetHeader->setHeader(UsbProtocol::USB_FRAME_HEADER);
    packetHeader->setCameraId(UsbProtocol::VIDEO_CAMERA_ID);
    packetHeader->setLength(50); 

    GetStream().send(packet);
    verifyNoValidFramesPublished();
}

TEST_F(MjpegStreamTest, IgnoresTruncatedMetadata) {
    std::vector<uint8_t> packet(10, 0x00); 
    auto* packetHeader = getPacketHeader(packet);
    packetHeader->setHeader(UsbProtocol::USB_FRAME_HEADER);
    packetHeader->setCameraId(UsbProtocol::VIDEO_CAMERA_ID);
    packetHeader->setLength(5); 

    GetStream().send(packet);
    verifyNoValidFramesPublished();
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

    GetStream().send(packet2);
}

TEST_F(MjpegStreamTest, SafelyHandlesGarbageDataWithoutCrashing) {
    FrameDisruptor ringBuffer;
    ringBuffer.preAllocate(Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);
    MjpegStream silentDecoder(ringBuffer);
    std::vector<uint8_t> packet(100, 0x00);
    
    ASSERT_NO_THROW(silentDecoder.send(packet));
}

TEST_F(MjpegStreamTest, PreventsIntegerUnderflowOnUndersizedHardwareLength) {
    std::vector<uint8_t> malformedPacket(TOTAL_USB_HEADER_SIZE, 0x00);
    
    auto* packetHeader = getPacketHeader(malformedPacket);
    packetHeader->setHeader(UsbProtocol::USB_FRAME_HEADER);
    packetHeader->setCameraId(UsbProtocol::VIDEO_CAMERA_ID);

    packetHeader->setLength(2);

    ASSERT_NO_THROW({
        GetStream().send(malformedPacket);
    });
}