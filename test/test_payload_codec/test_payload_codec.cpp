/**
 * @file test_payload_codec.cpp
 * @brief Covers empty ACKs, commands inside ACKs, command results, and opaque telemetry.
 */

#include <unity.h>

#include <packet_codec.h>

namespace {

void setStatus(proto::Status actual, proto::Status expected) {
    TEST_ASSERT_EQUAL_INT8(static_cast<int8_t>(expected), static_cast<int8_t>(actual));
}

proto::PacketHeader headerFor(proto::MessageType type, uint8_t src, uint8_t dst) {
    proto::PacketHeader header = {};
    header.type = type;
    header.srcId = src;
    header.dstId = dst;
    header.seq = 1;
    header.counter = 1;
    return header;
}

}  // namespace

void setUp(void) {}

void tearDown(void) {}

void test_empty_ack(void) {
    const proto::PacketCodec codec;
    uint8_t packet[proto::kMaxPhyBytes] = {};
    size_t packetLen = 0;
    const proto::PacketHeader header = headerFor(proto::MessageType::Ack, proto::kGatewayId, 3);

    setStatus(codec.encode(header, nullptr, 0, false, nullptr, packet, sizeof(packet), &packetLen),
              proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT8(0, packet[10]);

    proto::DecodedPacket decoded = {};
    setStatus(codec.decode(packet, packetLen, 3, false, &decoded), proto::Status::Ok);
    bool hasCommand = true;
    setStatus(codec.readAckPayload(decoded.payload, decoded.payloadLen, &hasCommand, nullptr),
              proto::Status::Ok);
    TEST_ASSERT_FALSE(hasCommand);
}

void test_ack_with_command_round_trip(void) {
    const proto::PacketCodec codec;
    const uint8_t body[] = {0xAA, 0xBB};
    uint8_t payload[proto::kMaxPhyBytes] = {};
    uint8_t payloadLen = 0;
    setStatus(codec.writeAckCommand(0x11223344, 0, body, sizeof(body), false, payload, sizeof(payload),
                                    &payloadLen),
              proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT8(8, payloadLen);
    TEST_ASSERT_EQUAL_UINT8(0x44, payload[0]);
    TEST_ASSERT_EQUAL_UINT8(0x33, payload[1]);
    TEST_ASSERT_EQUAL_UINT8(0x22, payload[2]);
    TEST_ASSERT_EQUAL_UINT8(0x11, payload[3]);
    TEST_ASSERT_EQUAL_UINT8(0, payload[4]);
    TEST_ASSERT_EQUAL_UINT8(2, payload[5]);

    uint8_t packet[proto::kMaxPhyBytes] = {};
    size_t packetLen = 0;
    const proto::PacketHeader header = headerFor(proto::MessageType::Ack, proto::kGatewayId, 3);
    setStatus(codec.encode(header, payload, payloadLen, false, nullptr, packet, sizeof(packet), &packetLen),
              proto::Status::Ok);

    proto::DecodedPacket decoded = {};
    setStatus(codec.decode(packet, packetLen, 3, false, &decoded), proto::Status::Ok);
    bool hasCommand = false;
    proto::AckCommand command = {};
    setStatus(codec.readAckPayload(decoded.payload, decoded.payloadLen, &hasCommand, &command),
              proto::Status::Ok);
    TEST_ASSERT_TRUE(hasCommand);
    TEST_ASSERT_EQUAL_UINT32(0x11223344, command.commandId);
    TEST_ASSERT_EQUAL_UINT8(0, command.commandType);
    TEST_ASSERT_EQUAL_UINT8(2, command.commandLen);
    TEST_ASSERT_EQUAL_UINT8(0xAA, command.body[0]);
    TEST_ASSERT_EQUAL_UINT8(0xBB, command.body[1]);
}

void test_command_len_overrun_is_rejected(void) {
    const proto::PacketCodec codec;
    uint8_t payload[7] = {};
    payload[5] = 2;
    bool hasCommand = false;
    proto::AckCommand command = {};
    setStatus(codec.readAckPayload(payload, sizeof(payload), &hasCommand, &command), proto::Status::BadPayload);
    TEST_ASSERT_FALSE(hasCommand);
}

void test_command_result_with_and_without_detail(void) {
    const proto::PacketCodec codec;
    uint8_t payload[proto::kMaxPhyBytes] = {};
    uint8_t payloadLen = 0;

    setStatus(codec.writeCommandResult(0x0A0B0C0D, static_cast<uint8_t>(proto::CommandResultStatus::Ok), nullptr, 0,
                                       false, payload, sizeof(payload), &payloadLen),
              proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT8(5, payloadLen);

    proto::CommandResultBody result = {};
    setStatus(codec.readCommandResult(payload, payloadLen, &result), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT32(0x0A0B0C0D, result.commandId);
    TEST_ASSERT_EQUAL_UINT8(0, result.status);
    TEST_ASSERT_EQUAL_UINT8(0, result.detailLen);
    TEST_ASSERT_NULL(result.detail);

    const uint8_t detail[] = {0x7E};
    setStatus(codec.writeCommandResult(9, static_cast<uint8_t>(proto::CommandResultStatus::Failed), detail, 1, false,
                                       payload, sizeof(payload), &payloadLen),
              proto::Status::Ok);
    setStatus(codec.readCommandResult(payload, payloadLen, &result), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT32(9, result.commandId);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(proto::CommandResultStatus::Failed), result.status);
    TEST_ASSERT_EQUAL_UINT8(1, result.detailLen);
    TEST_ASSERT_EQUAL_UINT8(0x7E, result.detail[0]);
}

void test_command_result_detail_must_fit_phy_budget(void) {
    const proto::PacketCodec codec;
    uint8_t detail[50] = {};
    uint8_t payload[proto::kMaxPhyBytes] = {};
    uint8_t payloadLen = 0;
    const uint8_t maxDetail = static_cast<uint8_t>(proto::kMaxPhyBytes - proto::kHeaderBytes - 5);

    setStatus(codec.writeCommandResult(1, 0, detail, maxDetail, false, payload, sizeof(payload), &payloadLen),
              proto::Status::Ok);
    setStatus(codec.writeCommandResult(1, 0, detail, static_cast<uint8_t>(maxDetail + 1), false, payload,
                                       sizeof(payload), &payloadLen),
              proto::Status::BadPayload);

    const uint8_t maxDetailAuth =
        static_cast<uint8_t>(proto::kMaxPhyBytes - proto::kHeaderBytes - proto::kAuthTagBytes - 5);
    setStatus(codec.writeCommandResult(1, 0, detail, static_cast<uint8_t>(maxDetailAuth + 1), true, payload,
                                       sizeof(payload), &payloadLen),
              proto::Status::BadPayload);
}

void test_empty_telemetry_is_valid_and_oversize_is_rejected(void) {
    const proto::PacketCodec codec;
    setStatus(codec.checkTelemetry(nullptr, 0, false), proto::Status::Ok);

    uint8_t packet[proto::kMaxPhyBytes] = {};
    size_t packetLen = 0;
    const proto::PacketHeader header = headerFor(proto::MessageType::Telemetry, 2, proto::kGatewayId);
    setStatus(codec.encode(header, nullptr, 0, false, nullptr, packet, sizeof(packet), &packetLen), proto::Status::Ok);

    proto::DecodedPacket decoded = {};
    setStatus(codec.decode(packet, packetLen, proto::kGatewayId, false, &decoded), proto::Status::Ok);
    setStatus(codec.checkTelemetry(decoded.payload, decoded.payloadLen, false), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT8(0, decoded.payloadLen);

    uint8_t blob[60] = {};
    const uint8_t maxPayload = static_cast<uint8_t>(proto::kMaxPhyBytes - proto::kHeaderBytes);
    setStatus(codec.checkTelemetry(blob, maxPayload, false), proto::Status::Ok);
    setStatus(codec.checkTelemetry(blob, static_cast<uint8_t>(maxPayload + 1), false), proto::Status::BadPayload);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_empty_ack);
    RUN_TEST(test_ack_with_command_round_trip);
    RUN_TEST(test_command_len_overrun_is_rejected);
    RUN_TEST(test_command_result_with_and_without_detail);
    RUN_TEST(test_command_result_detail_must_fit_phy_budget);
    RUN_TEST(test_empty_telemetry_is_valid_and_oversize_is_rejected);
    return UNITY_END();
}
