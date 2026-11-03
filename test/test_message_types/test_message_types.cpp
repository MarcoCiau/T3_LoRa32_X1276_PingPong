/**
 * @file test_message_types.cpp
 * @brief Locks draft protocol constants against the architecture notes.
 */

#include <unity.h>

#include <message_types.h>

void setUp(void) {}

void tearDown(void) {}

void test_version_and_sizes(void) {
    TEST_ASSERT_EQUAL_UINT8(1, proto::kVersion);
    TEST_ASSERT_EQUAL_UINT8(64, proto::kMaxPhyBytes);
    TEST_ASSERT_EQUAL_UINT8(11, proto::kHeaderBytes);
    TEST_ASSERT_EQUAL_UINT8(8, proto::kAuthTagBytes);
}

void test_message_type_ids(void) {
    TEST_ASSERT_EQUAL_UINT8(1, static_cast<uint8_t>(proto::MessageType::Telemetry));
    TEST_ASSERT_EQUAL_UINT8(2, static_cast<uint8_t>(proto::MessageType::Ack));
    TEST_ASSERT_EQUAL_UINT8(3, static_cast<uint8_t>(proto::MessageType::CommandResult));
    TEST_ASSERT_EQUAL_UINT8(4, static_cast<uint8_t>(proto::MessageType::Heartbeat));
    TEST_ASSERT_EQUAL_UINT8(5, static_cast<uint8_t>(proto::MessageType::PairRequest));
    TEST_ASSERT_EQUAL_UINT8(6, static_cast<uint8_t>(proto::MessageType::PairAccept));
    TEST_ASSERT_EQUAL_UINT8(7, static_cast<uint8_t>(proto::MessageType::PairReject));
    TEST_ASSERT_EQUAL_UINT8(8, static_cast<uint8_t>(proto::MessageType::Unpair));
    TEST_ASSERT_EQUAL_UINT8(9, static_cast<uint8_t>(proto::MessageType::Ping));
}

void test_id_limits(void) {
    TEST_ASSERT_EQUAL_UINT8(0, proto::kGatewayId);
    TEST_ASSERT_EQUAL_UINT8(1, proto::kMinNodeId);
    TEST_ASSERT_EQUAL_UINT8(10, proto::kMaxNodeId);
    TEST_ASSERT_EQUAL_UINT8(0xFF, proto::kUnassignedId);
    TEST_ASSERT_EQUAL_UINT8(6, proto::kMacBytes);
}

void test_status_codes(void) {
    TEST_ASSERT_EQUAL_INT8(0, static_cast<int8_t>(proto::Status::Ok));
    TEST_ASSERT_EQUAL_INT8(-1, static_cast<int8_t>(proto::Status::Truncated));
    TEST_ASSERT_EQUAL_INT8(-2, static_cast<int8_t>(proto::Status::BadVersion));
    TEST_ASSERT_EQUAL_INT8(-3, static_cast<int8_t>(proto::Status::BadType));
    TEST_ASSERT_EQUAL_INT8(-4, static_cast<int8_t>(proto::Status::BadDestination));
    TEST_ASSERT_EQUAL_INT8(-5, static_cast<int8_t>(proto::Status::BadAuth));
    TEST_ASSERT_EQUAL_INT8(-6, static_cast<int8_t>(proto::Status::Replay));
    TEST_ASSERT_EQUAL_INT8(-7, static_cast<int8_t>(proto::Status::Duplicate));
    TEST_ASSERT_EQUAL_INT8(-8, static_cast<int8_t>(proto::Status::BadPayload));
    TEST_ASSERT_EQUAL_INT8(-9, static_cast<int8_t>(proto::Status::Busy));
    TEST_ASSERT_EQUAL_INT8(-10, static_cast<int8_t>(proto::Status::Timeout));
}

void test_command_result_status_codes(void) {
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(proto::CommandResultStatus::Ok));
    TEST_ASSERT_EQUAL_UINT8(1, static_cast<uint8_t>(proto::CommandResultStatus::UnknownCommand));
    TEST_ASSERT_EQUAL_UINT8(2, static_cast<uint8_t>(proto::CommandResultStatus::Duplicate));
    TEST_ASSERT_EQUAL_UINT8(3, static_cast<uint8_t>(proto::CommandResultStatus::Failed));
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_version_and_sizes);
    RUN_TEST(test_message_type_ids);
    RUN_TEST(test_id_limits);
    RUN_TEST(test_status_codes);
    RUN_TEST(test_command_result_status_codes);
    return UNITY_END();
}
