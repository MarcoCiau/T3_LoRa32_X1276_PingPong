/**
 * @file test_node_command.cpp
 * @brief A new command id runs once, a repeat does not, and the result is on the radio.
 */

#include <unity.h>

#include <node_check_in.h>
#include <nop_command_executor.h>
#include <packet_codec.h>
#include <security.h>
#include <sequence_manager.h>

#include "../fakes/fake_clock.h"
#include "../fakes/fake_command_executor.h"
#include "../fakes/fake_radio.h"
#include "../fakes/fake_random.h"
#include "../fakes/memory_record_store.h"

namespace {

struct Fixture {
    FakeRadio radio;
    FakeClock clock;
    FakeRandom rng;
    proto::PacketCodec codec;
    proto::SequenceManager seq;
    MemoryRecordStore store;
    proto::ReplayWindow replay;
    proto::LabAuthOff auth;
    FakeCommandExecutor commands;
    node::NodeCheckIn checkIn;

    explicit Fixture(uint32_t intervalMs)
        : seq(rng),
          replay(store),
          checkIn(radio, clock, rng, seq, codec, replay, auth, commands, store, 1, intervalMs) {
        rng.next = 200;
    }
};

void setStatus(proto::Status actual, proto::Status expected) {
    TEST_ASSERT_EQUAL_INT8(static_cast<int8_t>(expected), static_cast<int8_t>(actual));
}

void queueAck(Fixture& fix, uint32_t counter, bool withCommand, uint32_t commandId, uint8_t commandType) {
    uint8_t payload[16] = {};
    uint8_t payloadLen = 0;
    const uint8_t* payloadPtr = nullptr;
    if (withCommand) {
        setStatus(fix.codec.writeAckCommand(commandId, commandType, nullptr, 0, false, payload, sizeof(payload), &payloadLen),
                  proto::Status::Ok);
        payloadPtr = payload;
    }
    proto::PacketHeader header = {};
    header.type = proto::MessageType::Ack;
    header.srcId = proto::kGatewayId;
    header.dstId = 1;
    header.seq = 1;
    header.counter = counter;
    uint8_t packet[proto::kMaxPhyBytes] = {};
    size_t len = 0;
    setStatus(fix.codec.encode(header, payloadPtr, payloadLen, false, nullptr, packet, sizeof(packet), &len),
              proto::Status::Ok);
    fix.radio.queueRx(packet, len);
}

proto::CommandResultBody txResult(Fixture& fix) {
    proto::DecodedPacket decoded = {};
    setStatus(fix.codec.decode(fix.radio.txData(), fix.radio.txLen(), proto::kGatewayId, false, &decoded),
              proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(proto::MessageType::CommandResult),
                            static_cast<uint8_t>(decoded.type));
    proto::CommandResultBody body = {};
    setStatus(fix.codec.readCommandResult(decoded.payload, decoded.payloadLen, &body), proto::Status::Ok);
    return body;
}

uint16_t txSeq(Fixture& fix) {
    proto::DecodedPacket decoded = {};
    setStatus(fix.codec.decode(fix.radio.txData(), fix.radio.txLen(), proto::kGatewayId, false, &decoded),
              proto::Status::Ok);
    return decoded.seq;
}

void expireWindow(Fixture& fix) {
    fix.clock.advance(node::NodeCheckIn::kAckWindowMs);
    setStatus(fix.checkIn.poll(), proto::Status::Ok);
}

void resendAfterDelay(Fixture& fix) {
    expireWindow(fix);
    TEST_ASSERT_FALSE(fix.checkIn.inIntervalWait());
    fix.clock.advance(200);
    setStatus(fix.checkIn.poll(), proto::Status::Ok);
}

void finishWithEmptyAck(Fixture& fix, uint32_t counter) {
    queueAck(fix, counter, false, 0, 0);
    setStatus(fix.checkIn.poll(), proto::Status::Ok);
    TEST_ASSERT_TRUE(fix.checkIn.inIntervalWait());
}

}  // namespace

void setUp(void) {}

void tearDown(void) {}

void test_nop_executor_accepts_only_type_zero(void) {
    NopCommandExecutor nop;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(proto::CommandResultStatus::Ok),
                            static_cast<uint8_t>(nop.execute(1, kNopCommandType, nullptr, 0)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(proto::CommandResultStatus::Failed),
                            static_cast<uint8_t>(nop.execute(1, 9, nullptr, 0)));
}

void test_new_id_runs_once_and_a_repeat_does_not(void) {
    Fixture fix(60000);
    setStatus(fix.checkIn.poll(), proto::Status::Ok);

    queueAck(fix, 1, true, 0x11, kNopCommandType);
    setStatus(fix.checkIn.poll(), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT32(1, fix.commands.calls);
    TEST_ASSERT_EQUAL_UINT32(0x11, fix.commands.lastId);
    TEST_ASSERT_EQUAL_UINT8(kNopCommandType, fix.commands.lastType);
    TEST_ASSERT_FALSE(fix.checkIn.inIntervalWait());

    const proto::CommandResultBody first = txResult(fix);
    TEST_ASSERT_EQUAL_UINT32(0x11, first.commandId);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(proto::CommandResultStatus::Ok), first.status);
    const uint16_t resultSeq = txSeq(fix);
    TEST_ASSERT_EQUAL_UINT16(2, resultSeq);

    finishWithEmptyAck(fix, 2);

    FakeCommandExecutor again;
    proto::SequenceManager seq2(fix.rng);
    proto::ReplayWindow replay2(fix.store);
    node::NodeCheckIn rebooted(fix.radio, fix.clock, fix.rng, seq2, fix.codec, replay2, fix.auth, again, fix.store, 1, 60000);
    fix.clock.advance(60000);
    setStatus(rebooted.poll(), proto::Status::Ok);
    queueAck(fix, 3, true, 0x11, kNopCommandType);
    setStatus(rebooted.poll(), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT32(0, again.calls);
    TEST_ASSERT_EQUAL_UINT32(1, fix.commands.calls);

    const proto::CommandResultBody second = txResult(fix);
    TEST_ASSERT_EQUAL_UINT32(0x11, second.commandId);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(proto::CommandResultStatus::Duplicate), second.status);
}

void test_unknown_command_does_not_call_the_executor(void) {
    Fixture fix(60000);
    setStatus(fix.checkIn.poll(), proto::Status::Ok);

    queueAck(fix, 1, true, 5, 9);
    setStatus(fix.checkIn.poll(), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT32(0, fix.commands.calls);
    TEST_ASSERT_FALSE(fix.checkIn.inIntervalWait());

    const proto::CommandResultBody body = txResult(fix);
    TEST_ASSERT_EQUAL_UINT32(5, body.commandId);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(proto::CommandResultStatus::UnknownCommand), body.status);
}

void test_result_ack_uses_the_retry_budget(void) {
    Fixture fix(60000);
    setStatus(fix.checkIn.poll(), proto::Status::Ok);
    queueAck(fix, 1, true, 7, kNopCommandType);
    setStatus(fix.checkIn.poll(), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT32(1, fix.commands.calls);
    TEST_ASSERT_EQUAL_UINT16(2, txSeq(fix));

    resendAfterDelay(fix);
    resendAfterDelay(fix);
    resendAfterDelay(fix);
    TEST_ASSERT_EQUAL_UINT32(1, fix.commands.calls);
    TEST_ASSERT_EQUAL_UINT16(5, fix.radio.txCount());
    TEST_ASSERT_EQUAL_UINT16(2, txSeq(fix));
    const proto::CommandResultBody body = txResult(fix);
    TEST_ASSERT_EQUAL_UINT32(7, body.commandId);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(proto::CommandResultStatus::Ok), body.status);

    expireWindow(fix);
    TEST_ASSERT_TRUE(fix.checkIn.inIntervalWait());
    TEST_ASSERT_EQUAL_UINT16(5, fix.radio.txCount());
    TEST_ASSERT_EQUAL_UINT32(1, fix.commands.calls);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_nop_executor_accepts_only_type_zero);
    RUN_TEST(test_new_id_runs_once_and_a_repeat_does_not);
    RUN_TEST(test_unknown_command_does_not_call_the_executor);
    RUN_TEST(test_result_ack_uses_the_retry_budget);
    return UNITY_END();
}
