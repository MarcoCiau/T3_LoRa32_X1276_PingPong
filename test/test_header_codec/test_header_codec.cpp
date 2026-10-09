/**
 * @file test_header_codec.cpp
 * @brief Round-trips the frame header and rejects truncated, mismatched, and unknown packets.
 */

#include <unity.h>

#include <packet_codec.h>

namespace {

void setStatus(proto::Status actual, proto::Status expected) {
    TEST_ASSERT_EQUAL_INT8(static_cast<int8_t>(expected), static_cast<int8_t>(actual));
}

proto::PacketHeader telemetryHeader() {
    proto::PacketHeader header = {};
    header.type = proto::MessageType::Telemetry;
    header.srcId = 1;
    header.dstId = proto::kGatewayId;
    header.seq = 0x0201;
    header.counter = 0x08070605;
    return header;
}

}  // namespace

void setUp(void) {}

void tearDown(void) {}

void test_telemetry_round_trip_and_little_endian(void) {
    const proto::PacketCodec codec;
    const uint8_t payload[] = {0x10, 0x20};
    uint8_t packet[proto::kMaxPhyBytes] = {};
    size_t packetLen = 0;

    setStatus(codec.encode(telemetryHeader(), payload, sizeof(payload), false, nullptr,
                           packet, sizeof(packet), &packetLen),
              proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT(11 + sizeof(payload), packetLen);
    TEST_ASSERT_EQUAL_UINT8(proto::kVersion, packet[0]);
    TEST_ASSERT_EQUAL_UINT8(1, packet[1]);
    TEST_ASSERT_EQUAL_UINT8(1, packet[2]);
    TEST_ASSERT_EQUAL_UINT8(0, packet[3]);
    TEST_ASSERT_EQUAL_UINT8(0x01, packet[4]);
    TEST_ASSERT_EQUAL_UINT8(0x02, packet[5]);
    TEST_ASSERT_EQUAL_UINT8(0x05, packet[6]);
    TEST_ASSERT_EQUAL_UINT8(0x06, packet[7]);
    TEST_ASSERT_EQUAL_UINT8(0x07, packet[8]);
    TEST_ASSERT_EQUAL_UINT8(0x08, packet[9]);
    TEST_ASSERT_EQUAL_UINT8(2, packet[10]);

    proto::DecodedPacket decoded = {};
    setStatus(codec.decode(packet, packetLen, proto::kGatewayId, false, &decoded), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(proto::MessageType::Telemetry),
                            static_cast<uint8_t>(decoded.type));
    TEST_ASSERT_EQUAL_UINT8(1, decoded.srcId);
    TEST_ASSERT_EQUAL_UINT8(0, decoded.dstId);
    TEST_ASSERT_EQUAL_UINT16(0x0201, decoded.seq);
    TEST_ASSERT_EQUAL_UINT32(0x08070605, decoded.counter);
    TEST_ASSERT_EQUAL_UINT8(2, decoded.payloadLen);
    TEST_ASSERT_EQUAL_UINT8(0x10, decoded.payload[0]);
    TEST_ASSERT_EQUAL_UINT8(0x20, decoded.payload[1]);
    TEST_ASSERT_NULL(decoded.tag);
}

void test_auth_tag_is_copied_and_not_checked(void) {
    const proto::PacketCodec codec;
    const uint8_t payload[] = {0xAA};
    const uint8_t tag[proto::kAuthTagBytes] = {9, 8, 7, 6, 5, 4, 3, 2};
    uint8_t packet[proto::kMaxPhyBytes] = {};
    size_t packetLen = 0;

    setStatus(codec.encode(telemetryHeader(), payload, 1, true, tag, packet, sizeof(packet), &packetLen),
              proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT(11 + 1 + proto::kAuthTagBytes, packetLen);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tag, packet + 12, proto::kAuthTagBytes);

    proto::DecodedPacket decoded = {};
    setStatus(codec.decode(packet, packetLen, proto::kGatewayId, true, &decoded), proto::Status::Ok);
    TEST_ASSERT_NOT_NULL(decoded.tag);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tag, decoded.tag, proto::kAuthTagBytes);
}

void test_auth_off_omits_tag(void) {
    const proto::PacketCodec codec;
    uint8_t packet[proto::kMaxPhyBytes] = {};
    size_t packetLen = 0;
    setStatus(codec.encode(telemetryHeader(), nullptr, 0, false, nullptr, packet, sizeof(packet), &packetLen),
              proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT(proto::kHeaderBytes, packetLen);
}

void test_truncated_buffer_is_rejected(void) {
    const proto::PacketCodec codec;
    uint8_t packet[proto::kMaxPhyBytes] = {};
    size_t packetLen = 0;
    const uint8_t payload[] = {1, 2, 3, 4};
    setStatus(codec.encode(telemetryHeader(), payload, sizeof(payload), false, nullptr, packet, sizeof(packet),
                           &packetLen),
              proto::Status::Ok);

    proto::DecodedPacket decoded = {};
    setStatus(codec.decode(packet, proto::kHeaderBytes - 1, proto::kGatewayId, false, &decoded),
              proto::Status::Truncated);
    setStatus(codec.decode(packet, proto::kHeaderBytes + 1, proto::kGatewayId, false, &decoded),
              proto::Status::Truncated);
}

void test_payload_len_longer_than_buffer_contents_is_rejected(void) {
    const proto::PacketCodec codec;
    uint8_t packet[proto::kMaxPhyBytes] = {};
    size_t packetLen = 0;
    const uint8_t payload[] = {1};
    setStatus(codec.encode(telemetryHeader(), payload, 1, false, nullptr, packet, sizeof(packet), &packetLen),
              proto::Status::Ok);
    packet[packetLen] = 0xFF;
    packet[packetLen + 1] = 0xFF;

    proto::DecodedPacket decoded = {};
    setStatus(codec.decode(packet, packetLen + 2, proto::kGatewayId, false, &decoded), proto::Status::BadPayload);
}

void test_unknown_version_and_type_are_rejected(void) {
    const proto::PacketCodec codec;
    uint8_t packet[proto::kMaxPhyBytes] = {};
    size_t packetLen = 0;
    setStatus(codec.encode(telemetryHeader(), nullptr, 0, false, nullptr, packet, sizeof(packet), &packetLen),
              proto::Status::Ok);

    proto::DecodedPacket decoded = {};
    packet[0] = 9;
    setStatus(codec.decode(packet, packetLen, proto::kGatewayId, false, &decoded), proto::Status::BadVersion);

    packet[0] = proto::kVersion;
    packet[1] = 0;
    setStatus(codec.decode(packet, packetLen, proto::kGatewayId, false, &decoded), proto::Status::BadType);
}

void test_wrong_destination_is_rejected_after_type(void) {
    const proto::PacketCodec codec;
    uint8_t packet[proto::kMaxPhyBytes] = {};
    size_t packetLen = 0;
    setStatus(codec.encode(telemetryHeader(), nullptr, 0, false, nullptr, packet, sizeof(packet), &packetLen),
              proto::Status::Ok);

    proto::DecodedPacket decoded = {};
    setStatus(codec.decode(packet, packetLen, 4, false, &decoded), proto::Status::BadDestination);

    packet[1] = 99;
    setStatus(codec.decode(packet, packetLen, 4, false, &decoded), proto::Status::BadType);
}

void test_payload_over_phy_budget_is_rejected(void) {
    const proto::PacketCodec codec;
    uint8_t payload[60] = {};
    uint8_t packet[80] = {};
    size_t packetLen = 0;
    setStatus(codec.encode(telemetryHeader(), payload, 54, false, nullptr, packet, sizeof(packet), &packetLen),
              proto::Status::BadPayload);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_telemetry_round_trip_and_little_endian);
    RUN_TEST(test_auth_tag_is_copied_and_not_checked);
    RUN_TEST(test_auth_off_omits_tag);
    RUN_TEST(test_truncated_buffer_is_rejected);
    RUN_TEST(test_payload_len_longer_than_buffer_contents_is_rejected);
    RUN_TEST(test_unknown_version_and_type_are_rejected);
    RUN_TEST(test_wrong_destination_is_rejected_after_type);
    RUN_TEST(test_payload_over_phy_budget_is_rejected);
    return UNITY_END();
}
