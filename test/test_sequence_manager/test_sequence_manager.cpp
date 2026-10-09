/**
 * @file test_sequence_manager.cpp
 * @brief Covers the retry budget, inclusive jitter bounds, and duplicate sequence numbers.
 */

#include <unity.h>

#include <sequence_manager.h>

#include "../fakes/fake_random.h"

namespace {

void setStatus(proto::Status actual, proto::Status expected) {
    TEST_ASSERT_EQUAL_INT8(static_cast<int8_t>(expected), static_cast<int8_t>(actual));
}

}  // namespace

void setUp(void) {}

void tearDown(void) {}

void test_retry_budget_and_inclusive_delay(void) {
    TEST_ASSERT_EQUAL_UINT8(3, proto::SequenceManager::kRetryBudget);
    TEST_ASSERT_EQUAL_UINT32(200, proto::SequenceManager::kRetryDelayMinMs);
    TEST_ASSERT_EQUAL_UINT32(800, proto::SequenceManager::kRetryDelayMaxMs);

    FakeRandom rng;
    proto::SequenceManager seq(rng);
    uint16_t value = 0;
    setStatus(seq.beginTransmit(proto::kGatewayId, &value), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT16(1, value);

    const uint32_t delays[] = {200, 500, 800};
    for (uint32_t delay : delays) {
        rng.next = delay;
        proto::RetryResult result = proto::RetryResult::WaitForInterval;
        uint16_t retrySeq = 0;
        uint32_t delayMs = 0;
        setStatus(seq.onTimeout(proto::kGatewayId, &result, &retrySeq, &delayMs), proto::Status::Ok);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(proto::RetryResult::Again), static_cast<uint8_t>(result));
        TEST_ASSERT_EQUAL_UINT16(1, retrySeq);
        TEST_ASSERT_EQUAL_UINT32(delay, delayMs);
        TEST_ASSERT_EQUAL_UINT32(200, rng.lastLo);
        TEST_ASSERT_EQUAL_UINT32(800, rng.lastHi);
    }
    TEST_ASSERT_EQUAL_UINT32(3, rng.calls);

    proto::RetryResult exhausted = proto::RetryResult::Again;
    uint16_t exhaustedSeq = 0;
    uint32_t exhaustedDelay = 99;
    setStatus(seq.onTimeout(proto::kGatewayId, &exhausted, &exhaustedSeq, &exhaustedDelay), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(proto::RetryResult::WaitForInterval),
                            static_cast<uint8_t>(exhausted));
    TEST_ASSERT_EQUAL_UINT32(0, exhaustedDelay);
    TEST_ASSERT_EQUAL_UINT32(3, rng.calls);

    setStatus(seq.beginTransmit(proto::kGatewayId, &value), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT16(2, value);
}

void test_each_destination_has_its_own_seq(void) {
    FakeRandom rng;
    proto::SequenceManager seq(rng);
    uint16_t toGateway = 0;
    uint16_t toNode = 0;
    setStatus(seq.beginTransmit(proto::kGatewayId, &toGateway), proto::Status::Ok);
    setStatus(seq.beginTransmit(4, &toNode), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT16(1, toGateway);
    TEST_ASSERT_EQUAL_UINT16(1, toNode);

    rng.next = 200;
    proto::RetryResult result = proto::RetryResult::WaitForInterval;
    uint16_t retrySeq = 0;
    uint32_t delayMs = 0;
    setStatus(seq.onTimeout(proto::kGatewayId, &result, &retrySeq, &delayMs), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT16(1, retrySeq);

    setStatus(seq.beginTransmit(4, &toNode), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT16(2, toNode);
}

void test_duplicate_seq_is_not_a_new_packet(void) {
    FakeRandom rng;
    proto::SequenceManager seq(rng);

    setStatus(seq.accept(1, 7), proto::Status::Ok);
    setStatus(seq.accept(1, 7), proto::Status::Duplicate);
    setStatus(seq.accept(1, 7), proto::Status::Duplicate);
    setStatus(seq.accept(1, 8), proto::Status::Ok);
    setStatus(seq.accept(2, 7), proto::Status::Ok);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_retry_budget_and_inclusive_delay);
    RUN_TEST(test_each_destination_has_its_own_seq);
    RUN_TEST(test_duplicate_seq_is_not_a_new_packet);
    return UNITY_END();
}
