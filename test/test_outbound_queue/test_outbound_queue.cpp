/**
 * @file test_outbound_queue.cpp
 * @brief A full uplink queue drops the oldest telemetry and keeps command results.
 */

#include <unity.h>

#include "outbound_queue.h"

namespace {

gateway::UplinkRecord telemetry(uint16_t seq) {
    gateway::UplinkRecord record = {};
    record.kind = gateway::UplinkKind::Telemetry;
    record.nodeId = 1;
    record.seq = seq;
    record.counter = seq;
    return record;
}

gateway::UplinkRecord result(uint32_t commandId) {
    gateway::UplinkRecord record = {};
    record.kind = gateway::UplinkKind::CommandResult;
    record.nodeId = 1;
    record.commandId = commandId;
    record.resultStatus = static_cast<uint8_t>(proto::CommandResultStatus::Ok);
    return record;
}

void setStatus(proto::Status actual, proto::Status expected) {
    TEST_ASSERT_EQUAL_INT8(static_cast<int8_t>(expected), static_cast<int8_t>(actual));
}

}  // namespace

void setUp(void) {}

void tearDown(void) {}

void test_pop_returns_mixed_records_in_arrival_order(void) {
    gateway::OutboundQueue queue;
    gateway::UplinkRecord out = {};
    TEST_ASSERT_FALSE(queue.pop(&out));
    TEST_ASSERT_FALSE(queue.pop(nullptr));

    setStatus(queue.push(telemetry(1)), proto::Status::Ok);
    setStatus(queue.push(result(9)), proto::Status::Ok);
    setStatus(queue.push(telemetry(2)), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT8(3, queue.size());
    TEST_ASSERT_EQUAL_UINT32(0, queue.droppedTelemetry());

    TEST_ASSERT_TRUE(queue.pop(&out));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(gateway::UplinkKind::Telemetry), static_cast<uint8_t>(out.kind));
    TEST_ASSERT_EQUAL_UINT16(1, out.seq);
    TEST_ASSERT_TRUE(queue.pop(&out));
    TEST_ASSERT_EQUAL_UINT32(9, out.commandId);
    TEST_ASSERT_TRUE(queue.pop(&out));
    TEST_ASSERT_EQUAL_UINT16(2, out.seq);
    TEST_ASSERT_EQUAL_UINT8(0, queue.size());
}

void test_full_telemetry_drops_the_oldest_after_the_head_moves(void) {
    gateway::OutboundQueue queue;
    for (uint16_t seq = 1; seq <= gateway::OutboundQueue::kCapacity; ++seq) {
        setStatus(queue.push(telemetry(seq)), proto::Status::Ok);
    }

    gateway::UplinkRecord out = {};
    TEST_ASSERT_TRUE(queue.pop(&out));
    TEST_ASSERT_EQUAL_UINT16(1, out.seq);
    TEST_ASSERT_TRUE(queue.pop(&out));
    TEST_ASSERT_EQUAL_UINT16(2, out.seq);
    TEST_ASSERT_TRUE(queue.pop(&out));
    TEST_ASSERT_EQUAL_UINT16(3, out.seq);

    setStatus(queue.push(telemetry(17)), proto::Status::Ok);
    setStatus(queue.push(telemetry(18)), proto::Status::Ok);
    setStatus(queue.push(telemetry(19)), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT8(gateway::OutboundQueue::kCapacity, queue.size());
    TEST_ASSERT_EQUAL_UINT32(0, queue.droppedTelemetry());

    setStatus(queue.push(telemetry(20)), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT32(1, queue.droppedTelemetry());
    TEST_ASSERT_EQUAL_UINT8(gateway::OutboundQueue::kCapacity, queue.size());

    TEST_ASSERT_TRUE(queue.pop(&out));
    TEST_ASSERT_EQUAL_UINT16(5, out.seq);
    for (uint16_t seq = 6; seq <= 20; ++seq) {
        TEST_ASSERT_TRUE(queue.pop(&out));
        TEST_ASSERT_EQUAL_UINT16(seq, out.seq);
    }
    TEST_ASSERT_FALSE(queue.pop(&out));
}

void test_results_fill_the_queue_and_telemetry_is_rejected(void) {
    gateway::OutboundQueue queue;
    for (uint32_t id = 1; id <= gateway::OutboundQueue::kCapacity; ++id) {
        setStatus(queue.push(result(id)), proto::Status::Ok);
    }

    setStatus(queue.push(telemetry(1)), proto::Status::Busy);
    TEST_ASSERT_EQUAL_UINT32(1, queue.droppedTelemetry());
    TEST_ASSERT_EQUAL_UINT8(gateway::OutboundQueue::kCapacity, queue.size());
    setStatus(queue.push(result(100)), proto::Status::Busy);
    TEST_ASSERT_EQUAL_UINT32(1, queue.droppedTelemetry());

    gateway::UplinkRecord out = {};
    for (uint32_t id = 1; id <= gateway::OutboundQueue::kCapacity; ++id) {
        TEST_ASSERT_TRUE(queue.pop(&out));
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(gateway::UplinkKind::CommandResult), static_cast<uint8_t>(out.kind));
        TEST_ASSERT_EQUAL_UINT32(id, out.commandId);
    }
}

void test_mixed_records_drop_telemetry_and_keep_every_result(void) {
    gateway::OutboundQueue queue;
    setStatus(queue.push(telemetry(1)), proto::Status::Ok);
    setStatus(queue.push(result(1)), proto::Status::Ok);
    setStatus(queue.push(telemetry(2)), proto::Status::Ok);
    setStatus(queue.push(result(2)), proto::Status::Ok);
    for (uint16_t seq = 3; seq <= 14; ++seq) {
        setStatus(queue.push(telemetry(seq)), proto::Status::Ok);
    }
    TEST_ASSERT_EQUAL_UINT8(gateway::OutboundQueue::kCapacity, queue.size());

    setStatus(queue.push(result(3)), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT32(1, queue.droppedTelemetry());
    setStatus(queue.push(telemetry(15)), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT32(2, queue.droppedTelemetry());

    const uint16_t telemetrySeq[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const uint32_t resultIds[] = {1, 2, 3};
    uint8_t telemetrySeen = 0;
    uint8_t resultsSeen = 0;
    gateway::UplinkRecord out = {};
    while (queue.pop(&out)) {
        if (out.kind == gateway::UplinkKind::Telemetry) {
            TEST_ASSERT_LESS_THAN_UINT8(sizeof(telemetrySeq) / sizeof(telemetrySeq[0]), telemetrySeen);
            TEST_ASSERT_EQUAL_UINT16(telemetrySeq[telemetrySeen], out.seq);
            telemetrySeen = static_cast<uint8_t>(telemetrySeen + 1);
        } else {
            TEST_ASSERT_LESS_THAN_UINT8(sizeof(resultIds) / sizeof(resultIds[0]), resultsSeen);
            TEST_ASSERT_EQUAL_UINT32(resultIds[resultsSeen], out.commandId);
            resultsSeen = static_cast<uint8_t>(resultsSeen + 1);
        }
    }
    TEST_ASSERT_EQUAL_UINT8(13, telemetrySeen);
    TEST_ASSERT_EQUAL_UINT8(3, resultsSeen);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_pop_returns_mixed_records_in_arrival_order);
    RUN_TEST(test_full_telemetry_drops_the_oldest_after_the_head_moves);
    RUN_TEST(test_results_fill_the_queue_and_telemetry_is_rejected);
    RUN_TEST(test_mixed_records_drop_telemetry_and_keep_every_result);
    return UNITY_END();
}
