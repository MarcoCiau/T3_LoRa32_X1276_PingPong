/**
 * @file test_record_store.cpp
 * @brief MemoryRecordStore keeps the previous blob when a commit is rejected.
 */

#include <unity.h>

#include <store_keys.h>

#include "../fakes/memory_record_store.h"

void setUp(void) {}

void tearDown(void) {}

void test_write_reread_and_overwrite(void) {
    MemoryRecordStore store;
    const uint16_t key = proto::lastCommandIdKey(3);
    const uint8_t first[] = {0x01, 0x02, 0x03, 0x04};
    const uint8_t second[] = {0x05, 0x06, 0x07, 0x08};
    uint8_t out[4] = {};

    TEST_ASSERT_TRUE(store.commit(key, first, sizeof(first)));
    TEST_ASSERT_TRUE(store.read(key, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(first, out, sizeof(first));

    TEST_ASSERT_TRUE(store.commit(key, second, sizeof(second)));
    TEST_ASSERT_TRUE(store.read(key, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(second, out, sizeof(second));
}

void test_failed_commit_keeps_previous_bytes(void) {
    MemoryRecordStore store;
    const uint16_t key = proto::replayCounterKey(1);
    const uint8_t stored[] = {0x0A, 0x0B, 0x0C, 0x0D};
    const uint8_t wrongSize[] = {0x01, 0x02};
    uint8_t out[4] = {};

    TEST_ASSERT_TRUE(store.commit(key, stored, sizeof(stored)));
    TEST_ASSERT_FALSE(store.commit(key, wrongSize, sizeof(wrongSize)));
    TEST_ASSERT_FALSE(store.commit(key, nullptr, sizeof(stored)));
    TEST_ASSERT_TRUE(store.read(key, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(stored, out, sizeof(stored));
}

void test_replay_and_command_keys_do_not_collide(void) {
    TEST_ASSERT_NOT_EQUAL(proto::replayCounterKey(1), proto::lastCommandIdKey(1));
    TEST_ASSERT_EQUAL_UINT16(proto::kReplayCounterKeyBase + 1, proto::replayCounterKey(1));
    TEST_ASSERT_EQUAL_UINT16(proto::kLastCommandIdKeyBase + 4, proto::lastCommandIdKey(4));
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_write_reread_and_overwrite);
    RUN_TEST(test_failed_commit_keeps_previous_bytes);
    RUN_TEST(test_replay_and_command_keys_do_not_collide);
    return UNITY_END();
}
