/**
 * @file test_node_check_in.cpp
 * @brief Node check-in succeeds on an empty Ack, retries the same seq, and ignores a truncated Ack.
 */

#include <unity.h>

#include <energy_payload.h>
#include <node_check_in.h>
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

void queueEmptyAck(Fixture& fix, uint32_t counter) {
    proto::PacketHeader header = {};
    header.type = proto::MessageType::Ack;
    header.srcId = proto::kGatewayId;
    header.dstId = 1;
    header.seq = 1;
    header.counter = counter;
    uint8_t packet[proto::kMaxPhyBytes] = {};
    size_t len = 0;
    setStatus(fix.codec.encode(header, nullptr, 0, false, nullptr, packet, sizeof(packet), &len), proto::Status::Ok);
    fix.radio.queueRx(packet, len);
}

uint16_t txSeq(Fixture& fix) {
    proto::DecodedPacket decoded = {};
    setStatus(fix.codec.decode(fix.radio.txData(), fix.radio.txLen(), proto::kGatewayId, false, &decoded),
              proto::Status::Ok);
    return decoded.seq;
}

proto::EnergyReading txEnergy(Fixture& fix) {
    proto::DecodedPacket decoded = {};
    setStatus(fix.codec.decode(fix.radio.txData(), fix.radio.txLen(), proto::kGatewayId, false, &decoded),
              proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT8(proto::kEnergyPayloadBytes, decoded.payloadLen);
    proto::EnergyReading reading = {};
    setStatus(proto::decodeEnergy(decoded.payload, decoded.payloadLen, &reading), proto::Status::Ok);
    return reading;
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

}  // namespace

void setUp(void) {}

void tearDown(void) {}

void test_first_report_carries_mock_energy_for_node_1_seq_1(void) {
    Fixture fix(60000);
    setStatus(fix.checkIn.poll(), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT16(1, txSeq(fix));
    const proto::EnergyReading first = txEnergy(fix);
    TEST_ASSERT_EQUAL_UINT16(23010, first.voltageCentivolts);
    TEST_ASSERT_EQUAL_UINT16(1001, first.currentMilliamps);
    TEST_ASSERT_EQUAL_UINT16(101, first.powerWatts);
    TEST_ASSERT_EQUAL_UINT32(1001, first.energyWattHours);
    TEST_ASSERT_EQUAL_UINT16(950, first.powerFactorMilli);
    TEST_ASSERT_EQUAL_UINT16(5000, first.frequencyCentiHertz);

    resendAfterDelay(fix);
    TEST_ASSERT_EQUAL_UINT16(1, txSeq(fix));
    const proto::EnergyReading retry = txEnergy(fix);
    TEST_ASSERT_EQUAL_UINT16(first.voltageCentivolts, retry.voltageCentivolts);
    TEST_ASSERT_EQUAL_UINT16(first.currentMilliamps, retry.currentMilliamps);
    TEST_ASSERT_EQUAL_UINT32(first.energyWattHours, retry.energyWattHours);
}

void test_empty_ack_ends_the_cycle(void) {
    Fixture fix(60000);
    setStatus(fix.checkIn.poll(), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT16(1, fix.radio.txCount());
    TEST_ASSERT_EQUAL_UINT16(1, txSeq(fix));
    TEST_ASSERT_FALSE(fix.checkIn.inIntervalWait());

    queueEmptyAck(fix, 1);
    setStatus(fix.checkIn.poll(), proto::Status::Ok);
    TEST_ASSERT_TRUE(fix.checkIn.inIntervalWait());
    TEST_ASSERT_EQUAL_UINT16(1, fix.radio.txCount());
}

void test_one_retry_then_success(void) {
    Fixture fix(60000);
    setStatus(fix.checkIn.poll(), proto::Status::Ok);
    resendAfterDelay(fix);
    TEST_ASSERT_EQUAL_UINT16(2, fix.radio.txCount());
    TEST_ASSERT_EQUAL_UINT16(1, txSeq(fix));

    queueEmptyAck(fix, 1);
    setStatus(fix.checkIn.poll(), proto::Status::Ok);
    TEST_ASSERT_TRUE(fix.checkIn.inIntervalWait());
    TEST_ASSERT_EQUAL_UINT16(2, fix.radio.txCount());
}

void test_budget_exhaustion_waits_for_the_interval(void) {
    Fixture fix(60000);
    setStatus(fix.checkIn.poll(), proto::Status::Ok);
    resendAfterDelay(fix);
    resendAfterDelay(fix);
    resendAfterDelay(fix);
    TEST_ASSERT_EQUAL_UINT16(4, fix.radio.txCount());
    TEST_ASSERT_EQUAL_UINT16(1, txSeq(fix));

    expireWindow(fix);
    TEST_ASSERT_TRUE(fix.checkIn.inIntervalWait());
    TEST_ASSERT_EQUAL_UINT16(4, fix.radio.txCount());

    fix.clock.advance(59999);
    setStatus(fix.checkIn.poll(), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT16(4, fix.radio.txCount());

    fix.clock.advance(1);
    setStatus(fix.checkIn.poll(), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT16(5, fix.radio.txCount());
    TEST_ASSERT_EQUAL_UINT16(2, txSeq(fix));
}

void test_truncated_ack_does_not_succeed_or_reset_the_window(void) {
    Fixture fix(60000);
    setStatus(fix.checkIn.poll(), proto::Status::Ok);

    const uint8_t truncated[] = {0x01, 0x02, 0x03};
    fix.radio.queueRx(truncated, sizeof(truncated));
    fix.clock.advance(100);
    setStatus(fix.checkIn.poll(), proto::Status::Ok);
    TEST_ASSERT_FALSE(fix.checkIn.inIntervalWait());
    TEST_ASSERT_EQUAL_UINT16(1, fix.radio.txCount());

    fix.clock.advance(399);
    setStatus(fix.checkIn.poll(), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT16(1, fix.radio.txCount());
    TEST_ASSERT_FALSE(fix.checkIn.inIntervalWait());

    fix.clock.advance(1);
    setStatus(fix.checkIn.poll(), proto::Status::Ok);
    TEST_ASSERT_FALSE(fix.checkIn.inIntervalWait());
    TEST_ASSERT_EQUAL_UINT16(1, fix.radio.txCount());

    fix.clock.advance(200);
    setStatus(fix.checkIn.poll(), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT16(2, fix.radio.txCount());
    TEST_ASSERT_EQUAL_UINT16(1, txSeq(fix));
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_first_report_carries_mock_energy_for_node_1_seq_1);
    RUN_TEST(test_empty_ack_ends_the_cycle);
    RUN_TEST(test_one_retry_then_success);
    RUN_TEST(test_budget_exhaustion_waits_for_the_interval);
    RUN_TEST(test_truncated_ack_does_not_succeed_or_reset_the_window);
    return UNITY_END();
}
