/**
 * @file test_security.cpp
 * @brief Lab auth omits the tag, and the replay window accepts only a newer counter.
 */

#include <unity.h>

#include <packet_codec.h>
#include <security.h>

#include <cstring>

#include "../fakes/memory_record_store.h"

namespace {

void setStatus(proto::Status actual, proto::Status expected) {
    TEST_ASSERT_EQUAL_INT8(static_cast<int8_t>(expected), static_cast<int8_t>(actual));
}

}  // namespace

void setUp(void) {}

void tearDown(void) {}

void test_counter_must_be_strictly_newer(void) {
    MemoryRecordStore store;
    proto::ReplayWindow first(store);

    setStatus(first.accept(1, 1), proto::Status::Ok);
    setStatus(first.accept(1, 2), proto::Status::Ok);
    setStatus(first.accept(1, 2), proto::Status::Replay);
    setStatus(first.accept(1, 1), proto::Status::Replay);

    proto::ReplayWindow reloaded(store);
    setStatus(reloaded.accept(1, 2), proto::Status::Replay);
    setStatus(reloaded.accept(1, 3), proto::Status::Ok);
    setStatus(reloaded.accept(2, 1), proto::Status::Ok);
}

void test_lab_packet_has_no_tag_bytes(void) {
    const proto::LabAuthOff auth;
    TEST_ASSERT_EQUAL_UINT8(0, auth.tagLen());

    uint8_t tag[proto::kAuthTagBytes];
    std::memset(tag, 0x5A, sizeof(tag));
    const uint8_t frame[] = {0x01, 0x02, 0x03};
    setStatus(auth.sign(frame, sizeof(frame), tag), proto::Status::Ok);
    setStatus(auth.verify(frame, sizeof(frame), nullptr), proto::Status::Ok);
    for (uint8_t byte : tag) {
        TEST_ASSERT_EQUAL_UINT8(0x5A, byte);
    }

    const proto::PacketCodec codec;
    proto::PacketHeader header = {};
    header.type = proto::MessageType::Telemetry;
    header.srcId = 1;
    header.dstId = proto::kGatewayId;
    header.seq = 1;
    header.counter = 2;
    const uint8_t payload[] = {0x11};
    uint8_t packet[proto::kMaxPhyBytes] = {};
    size_t packetLen = 0;
    const bool authOn = auth.tagLen() != 0;
    setStatus(codec.encode(header, payload, sizeof(payload), authOn, nullptr, packet, sizeof(packet), &packetLen),
              proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT(proto::kHeaderBytes + sizeof(payload), packetLen);
    TEST_ASSERT_FALSE(authOn);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_counter_must_be_strictly_newer);
    RUN_TEST(test_lab_packet_has_no_tag_bytes);
    return UNITY_END();
}
