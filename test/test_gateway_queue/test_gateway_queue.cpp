/**
 * @file test_gateway_queue.cpp
 * @brief The registry records a node, and the queue reloads both commands after a new object is built.
 */

#include <unity.h>

#include "command_queue.h"
#include "node_registry.h"

#include "../fakes/memory_record_store.h"

#include <cstdint>

namespace {

void setStatus(proto::Status actual, proto::Status expected) {
    TEST_ASSERT_EQUAL_INT8(static_cast<int8_t>(expected), static_cast<int8_t>(actual));
}

}  // namespace

void setUp(void) {}

void tearDown(void) {}

void test_registry_keeps_each_node(void) {
    gateway::NodeRegistry registry;
    gateway::NodeFacts facts = {};
    TEST_ASSERT_FALSE(registry.lookup(1, &facts));
    setStatus(registry.observe(0, 1, 1, -40.0f, 8.0f, 10), proto::Status::BadDestination);
    setStatus(registry.observe(1, 4, 2, -40.0f, 8.0f, 1000), proto::Status::Ok);
    setStatus(registry.observe(2, 9, 3, -12.5f, 5.5f, 2000), proto::Status::Ok);

    TEST_ASSERT_TRUE(registry.lookup(1, &facts));
    TEST_ASSERT_EQUAL_UINT32(4, facts.counter);
    TEST_ASSERT_EQUAL_UINT16(2, facts.seq);
    TEST_ASSERT_EQUAL_FLOAT(-40.0f, facts.rssi);
    TEST_ASSERT_EQUAL_FLOAT(8.0f, facts.snr);
    TEST_ASSERT_EQUAL_UINT32(1000, facts.lastSeenMs);

    TEST_ASSERT_TRUE(registry.lookup(2, &facts));
    TEST_ASSERT_EQUAL_UINT32(9, facts.counter);
    TEST_ASSERT_EQUAL_UINT16(3, facts.seq);
    TEST_ASSERT_EQUAL_UINT32(2000, facts.lastSeenMs);
    setStatus(registry.observe(11, 1, 1, 0.0f, 0.0f, 1), proto::Status::BadDestination);
}

void test_two_commands_survive_reload_and_only_the_first_is_handed_out(void) {
    MemoryRecordStore store;
    gateway::CommandQueue queue(store);
    const uint8_t body[] = {0xAB, 0xCD};
    uint32_t firstId = 0;
    uint32_t secondId = 0;
    setStatus(queue.enqueue(1, 0, body, sizeof(body), &firstId), proto::Status::Ok);
    setStatus(queue.enqueue(1, 4, nullptr, 0, &secondId), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT32(1, firstId);
    TEST_ASSERT_EQUAL_UINT32(2, secondId);
    TEST_ASSERT_EQUAL_UINT8(2, queue.size());

    gateway::QueuedCommand handed = {};
    TEST_ASSERT_TRUE(queue.nextFor(1, &handed));
    TEST_ASSERT_EQUAL_UINT32(firstId, handed.commandId);
    TEST_ASSERT_EQUAL_UINT8(1, handed.nodeId);
    TEST_ASSERT_EQUAL_UINT8(0, handed.commandType);
    TEST_ASSERT_EQUAL_UINT8(2, handed.bodyLen);
    TEST_ASSERT_EQUAL_UINT8(0xAB, handed.body[0]);
    TEST_ASSERT_EQUAL_UINT8(0xCD, handed.body[1]);
    TEST_ASSERT_FALSE(queue.nextFor(2, nullptr));

    gateway::CommandQueue reloaded(store);
    TEST_ASSERT_EQUAL_UINT8(2, reloaded.size());
    gateway::QueuedCommand again = {};
    TEST_ASSERT_TRUE(reloaded.nextFor(1, &again));
    TEST_ASSERT_EQUAL_UINT32(firstId, again.commandId);
    TEST_ASSERT_EQUAL_UINT8(0xAB, again.body[0]);

    gateway::QueuedCommand second = {};
    TEST_ASSERT_TRUE(reloaded.find(secondId, &second));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(gateway::CommandState::Pending), static_cast<uint8_t>(second.state));
    TEST_ASSERT_EQUAL_UINT8(4, second.commandType);

    setStatus(reloaded.markDelivered(firstId), proto::Status::Ok);
    TEST_ASSERT_TRUE(reloaded.nextFor(1, &handed));
    TEST_ASSERT_EQUAL_UINT32(secondId, handed.commandId);

    gateway::CommandQueue afterDelivery(store);
    TEST_ASSERT_TRUE(afterDelivery.find(firstId, &handed));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(gateway::CommandState::Delivered),
                            static_cast<uint8_t>(handed.state));
    TEST_ASSERT_TRUE(afterDelivery.nextFor(1, &handed));
    TEST_ASSERT_EQUAL_UINT32(secondId, handed.commandId);
    setStatus(afterDelivery.markResult(firstId, proto::CommandResultStatus::Ok), proto::Status::Ok);
    TEST_ASSERT_TRUE(afterDelivery.find(firstId, &handed));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(gateway::CommandState::Succeeded),
                            static_cast<uint8_t>(handed.state));
}

void test_supplied_id_is_kept_and_the_ninth_command_is_rejected(void) {
    MemoryRecordStore store;
    gateway::CommandQueue queue(store);
    setStatus(queue.enqueueWithId(3, 0, nullptr, 0, 40), proto::Status::Ok);
    uint32_t assigned = 0;
    setStatus(queue.enqueue(3, 0, nullptr, 0, &assigned), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT32(41, assigned);
    setStatus(queue.enqueueWithId(3, 0, nullptr, 0, 40), proto::Status::Duplicate);

    gateway::CommandQueue reloaded(store);
    gateway::QueuedCommand handed = {};
    TEST_ASSERT_TRUE(reloaded.nextFor(3, &handed));
    TEST_ASSERT_EQUAL_UINT32(40, handed.commandId);
    TEST_ASSERT_TRUE(reloaded.find(41, &handed));

    MemoryRecordStore fullStore;
    gateway::CommandQueue full(fullStore);
    for (uint8_t i = 0; i < gateway::kCommandCapacity; ++i) {
        setStatus(full.enqueue(1, 0, nullptr, 0, nullptr), proto::Status::Ok);
    }
    setStatus(full.enqueue(1, 0, nullptr, 0, nullptr), proto::Status::Busy);
    TEST_ASSERT_EQUAL_UINT8(gateway::kCommandCapacity, full.size());
    const uint8_t tooLong[gateway::kCommandBodyBytes + 1] = {};
    setStatus(full.enqueue(1, 0, tooLong, sizeof(tooLong), nullptr), proto::Status::BadPayload);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_registry_keeps_each_node);
    RUN_TEST(test_two_commands_survive_reload_and_only_the_first_is_handed_out);
    RUN_TEST(test_supplied_id_is_kept_and_the_ninth_command_is_rejected);
    return UNITY_END();
}
