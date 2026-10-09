/**
 * @file test_security.cpp
 * @brief Lab auth omits the tag. AES-CCM matches a known vector. Replay still rejects a reused counter.
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

/** @brief AES-128 key for the lab vector. Not proto::kDeploymentKey, and not sent on the air. */
constexpr uint8_t kVectorKey[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

/**
 * @brief Header and one payload byte.
 *
 * version 1, telemetry, src 1, dst 0, seq 1, counter 2, payload_len 1, payload 0x11.
 * The nonce is src 1, counter 2 little-endian, and eight zero bytes.
 */
constexpr uint8_t kVectorFrame[] = {0x01, 0x01, 0x01, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x11};

/** @brief AES-CCM tag for kVectorKey and kVectorFrame. The payload is not encrypted. */
constexpr uint8_t kVectorTag[proto::kAuthTagBytes] = {0x52, 0x6b, 0xbf, 0x82, 0xe6, 0x67, 0xec, 0xfa};

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

void test_aes_ccm_vector_matches(void) {
    const proto::AesCcmAuth auth(kVectorKey);
    TEST_ASSERT_EQUAL_UINT8(proto::kAuthTagBytes, auth.tagLen());

    uint8_t tag[proto::kAuthTagBytes] = {};
    setStatus(auth.sign(kVectorFrame, sizeof(kVectorFrame), tag), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kVectorTag, tag, proto::kAuthTagBytes);
    setStatus(auth.verify(kVectorFrame, sizeof(kVectorFrame), tag), proto::Status::Ok);

    uint8_t longer[31] = {};
    longer[0] = 0x01;
    longer[1] = 0x01;
    longer[2] = 0x01;
    longer[4] = 0x01;
    longer[6] = 0x02;
    longer[10] = 20;
    for (uint8_t i = 0; i < 20; ++i) {
        longer[11 + i] = i;
    }
    const uint8_t longerTag[proto::kAuthTagBytes] = {0x8e, 0x71, 0x99, 0xaf, 0xb5, 0x0a, 0xf6, 0x24};
    setStatus(auth.sign(longer, sizeof(longer), tag), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(longerTag, tag, proto::kAuthTagBytes);
}

void test_flipped_tag_byte_is_bad_auth(void) {
    const proto::AesCcmAuth auth(kVectorKey);
    uint8_t tag[proto::kAuthTagBytes];
    std::memcpy(tag, kVectorTag, sizeof(tag));
    tag[0] = static_cast<uint8_t>(tag[0] ^ 0x01);
    setStatus(auth.verify(kVectorFrame, sizeof(kVectorFrame), tag), proto::Status::BadAuth);

    MemoryRecordStore store;
    proto::ReplayWindow window(store);
    setStatus(window.accept(1, 2), proto::Status::Ok);
    setStatus(window.accept(1, 2), proto::Status::Replay);
}

void test_replay_rejects_a_reused_counter_when_auth_is_on(void) {
    const proto::AesCcmAuth auth(kVectorKey);
    uint8_t first[sizeof(kVectorFrame)];
    std::memcpy(first, kVectorFrame, sizeof(first));
    first[6] = 1;
    uint8_t second[sizeof(kVectorFrame)];
    std::memcpy(second, kVectorFrame, sizeof(second));
    second[6] = 2;

    uint8_t firstTag[proto::kAuthTagBytes];
    uint8_t secondTag[proto::kAuthTagBytes];
    setStatus(auth.sign(first, sizeof(first), firstTag), proto::Status::Ok);
    setStatus(auth.sign(second, sizeof(second), secondTag), proto::Status::Ok);

    MemoryRecordStore store;
    proto::ReplayWindow window(store);
    setStatus(auth.verify(first, sizeof(first), firstTag), proto::Status::Ok);
    setStatus(window.accept(first[2], 1), proto::Status::Ok);
    setStatus(auth.verify(first, sizeof(first), firstTag), proto::Status::Ok);
    setStatus(window.accept(first[2], 1), proto::Status::Replay);
    setStatus(auth.verify(second, sizeof(second), secondTag), proto::Status::Ok);
    setStatus(window.accept(second[2], 2), proto::Status::Ok);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_counter_must_be_strictly_newer);
    RUN_TEST(test_lab_packet_has_no_tag_bytes);
    RUN_TEST(test_aes_ccm_vector_matches);
    RUN_TEST(test_flipped_tag_byte_is_bad_auth);
    RUN_TEST(test_replay_rejects_a_reused_counter_when_auth_is_on);
    return UNITY_END();
}
