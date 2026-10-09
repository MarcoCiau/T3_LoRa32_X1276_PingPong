/**
 * @file test_gateway_coordinator.cpp
 * @brief Telemetry gets an Ack, a duplicate does not uplink twice, and a result completes the command.
 */

#include <unity.h>

#include "command_queue.h"
#include "node_registry.h"
#include "radio_service.h"

#include "../fakes/fake_clock.h"
#include "../fakes/fake_outbound_buffer.h"
#include "../fakes/fake_radio.h"
#include "../fakes/fake_random.h"
#include "../fakes/memory_record_store.h"

#include <energy_payload.h>
#include <packet_codec.h>
#include <security.h>
#include <sequence_manager.h>

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
    gateway::NodeRegistry registry;
    gateway::CommandQueue commands;
    FakeOutboundBuffer uplink;
    gateway::RadioService service;

    Fixture()
        : seq(rng),
          replay(store),
          commands(store),
          service(radio, clock, codec, seq, replay, auth, registry, commands, uplink) {
        clock.now = 5000;
    }
};

void setStatus(proto::Status actual, proto::Status expected) {
    TEST_ASSERT_EQUAL_INT8(static_cast<int8_t>(expected), static_cast<int8_t>(actual));
}

void queueFrame(Fixture& fix,
                proto::MessageType type,
                uint16_t seq,
                uint32_t counter,
                const uint8_t* payload,
                uint8_t payloadLen,
                uint8_t srcId = 1) {
    proto::PacketHeader header = {};
    header.type = type;
    header.srcId = srcId;
    header.dstId = proto::kGatewayId;
    header.seq = seq;
    header.counter = counter;
    uint8_t packet[proto::kMaxPhyBytes] = {};
    size_t len = 0;
    setStatus(fix.codec.encode(header, payload, payloadLen, false, nullptr, packet, sizeof(packet), &len), proto::Status::Ok);
    fix.radio.queueRx(packet, len);
}

void queueTelemetry(Fixture& fix, uint16_t seq, uint32_t counter) {
    queueFrame(fix, proto::MessageType::Telemetry, seq, counter, nullptr, 0);
}

void queueResult(Fixture& fix, uint16_t seq, uint32_t counter, uint32_t commandId, proto::CommandResultStatus status) {
    uint8_t payload[8] = {};
    uint8_t payloadLen = 0;
    setStatus(fix.codec.writeCommandResult(commandId, static_cast<uint8_t>(status), nullptr, 0, false, payload, sizeof(payload), &payloadLen),
              proto::Status::Ok);
    queueFrame(fix, proto::MessageType::CommandResult, seq, counter, payload, payloadLen);
}

proto::DecodedPacket txPacket(Fixture& fix) {
    proto::DecodedPacket decoded = {};
    setStatus(fix.codec.decode(fix.radio.txData(), fix.radio.txLen(), 1, false, &decoded), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(proto::MessageType::Ack), static_cast<uint8_t>(decoded.type));
    TEST_ASSERT_EQUAL_UINT8(proto::kGatewayId, decoded.srcId);
    TEST_ASSERT_EQUAL_UINT8(1, decoded.dstId);
    return decoded;
}

}  // namespace

void setUp(void) {}

void tearDown(void) {}

void test_energy_body_is_stored_and_an_empty_body_is_still_acked(void) {
    Fixture fix;
    proto::EnergyReading reading = {};
    reading.voltageCentivolts = 23020;
    reading.currentMilliamps = 1001;
    reading.powerWatts = 102;
    reading.energyWattHours = 2001;
    reading.powerFactorMilli = 950;
    reading.frequencyCentiHertz = 5000;
    uint8_t body[proto::kEnergyPayloadBytes] = {};
    setStatus(proto::encodeEnergy(reading, body, sizeof(body)), proto::Status::Ok);
    queueFrame(fix, proto::MessageType::Telemetry, 1, 1, body, sizeof(body), 2);

    setStatus(fix.service.poll(), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT16(1, fix.radio.txCount());
    TEST_ASSERT_EQUAL_UINT8(1, fix.uplink.count);
    TEST_ASSERT_TRUE(fix.uplink.records[0].hasEnergy);
    TEST_ASSERT_EQUAL_UINT8(2, fix.uplink.records[0].nodeId);
    TEST_ASSERT_EQUAL_UINT16(23020, fix.uplink.records[0].energy.voltageCentivolts);
    TEST_ASSERT_EQUAL_UINT16(1001, fix.uplink.records[0].energy.currentMilliamps);
    TEST_ASSERT_EQUAL_UINT16(102, fix.uplink.records[0].energy.powerWatts);
    TEST_ASSERT_EQUAL_UINT32(2001, fix.uplink.records[0].energy.energyWattHours);
    TEST_ASSERT_EQUAL_UINT16(950, fix.uplink.records[0].energy.powerFactorMilli);
    TEST_ASSERT_EQUAL_UINT16(5000, fix.uplink.records[0].energy.frequencyCentiHertz);

    gateway::NodeFacts facts = {};
    TEST_ASSERT_TRUE(fix.registry.lookup(2, &facts));
    TEST_ASSERT_TRUE(facts.hasEnergy);
    TEST_ASSERT_EQUAL_UINT16(23020, facts.energy.voltageCentivolts);

    proto::DecodedPacket ack = {};
    setStatus(fix.codec.decode(fix.radio.txData(), fix.radio.txLen(), 2, false, &ack), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(proto::MessageType::Ack), static_cast<uint8_t>(ack.type));
    TEST_ASSERT_EQUAL_UINT8(0, ack.payloadLen);

    queueTelemetry(fix, 1, 1);
    setStatus(fix.service.poll(), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT16(2, fix.radio.txCount());
    TEST_ASSERT_EQUAL_UINT8(2, fix.uplink.count);
    TEST_ASSERT_FALSE(fix.uplink.records[1].hasEnergy);
    TEST_ASSERT_EQUAL_UINT8(1, fix.uplink.records[1].nodeId);
    const proto::DecodedPacket emptyAck = txPacket(fix);
    TEST_ASSERT_EQUAL_UINT8(0, emptyAck.payloadLen);

    const uint8_t unknown[] = {0x02, 0x00, 0x00};
    queueFrame(fix, proto::MessageType::Telemetry, 1, 1, unknown, sizeof(unknown), 3);
    setStatus(fix.service.poll(), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT16(3, fix.radio.txCount());
    TEST_ASSERT_EQUAL_UINT8(3, fix.uplink.count);
    TEST_ASSERT_FALSE(fix.uplink.records[2].hasEnergy);
    TEST_ASSERT_TRUE(fix.registry.lookup(3, &facts));
    TEST_ASSERT_FALSE(facts.hasEnergy);
}

void test_telemetry_gets_an_empty_ack_and_one_uplink(void) {
    Fixture fix;
    setStatus(fix.service.poll(), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT16(0, fix.radio.txCount());

    const uint8_t truncated[] = {0x01, 0x02, 0x03};
    fix.radio.queueRx(truncated, sizeof(truncated));
    setStatus(fix.service.poll(), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT16(0, fix.radio.txCount());
    TEST_ASSERT_EQUAL_UINT8(0, fix.uplink.count);

    queueTelemetry(fix, 1, 1);
    setStatus(fix.service.poll(), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT16(1, fix.radio.txCount());
    TEST_ASSERT_EQUAL_UINT8(1, fix.uplink.count);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(gateway::UplinkKind::Telemetry), static_cast<uint8_t>(fix.uplink.records[0].kind));
    TEST_ASSERT_EQUAL_UINT8(1, fix.uplink.records[0].nodeId);
    TEST_ASSERT_EQUAL_UINT16(1, fix.uplink.records[0].seq);
    TEST_ASSERT_EQUAL_UINT32(1, fix.uplink.records[0].counter);

    const proto::DecodedPacket ack = txPacket(fix);
    TEST_ASSERT_EQUAL_UINT8(0, ack.payloadLen);
    TEST_ASSERT_EQUAL_UINT32(1, ack.counter);

    gateway::NodeFacts facts = {};
    TEST_ASSERT_TRUE(fix.registry.lookup(1, &facts));
    TEST_ASSERT_EQUAL_UINT32(1, facts.counter);
    TEST_ASSERT_EQUAL_UINT16(1, facts.seq);
    TEST_ASSERT_EQUAL_FLOAT(-40.0f, facts.rssi);
    TEST_ASSERT_EQUAL_FLOAT(8.0f, facts.snr);
    TEST_ASSERT_EQUAL_UINT32(5000, facts.lastSeenMs);
}

void test_pending_command_is_delivered_in_the_ack(void) {
    Fixture fix;
    uint32_t firstId = 0;
    uint32_t secondId = 0;
    const uint8_t body[] = {0x10};
    setStatus(fix.commands.enqueue(1, 0, body, sizeof(body), &firstId), proto::Status::Ok);
    setStatus(fix.commands.enqueue(1, 0, nullptr, 0, &secondId), proto::Status::Ok);

    queueTelemetry(fix, 1, 1);
    setStatus(fix.service.poll(), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT8(1, fix.uplink.count);

    const proto::DecodedPacket ack = txPacket(fix);
    bool hasCommand = false;
    proto::AckCommand command = {};
    setStatus(fix.codec.readAckPayload(ack.payload, ack.payloadLen, &hasCommand, &command), proto::Status::Ok);
    TEST_ASSERT_TRUE(hasCommand);
    TEST_ASSERT_EQUAL_UINT32(firstId, command.commandId);
    TEST_ASSERT_EQUAL_UINT8(0, command.commandType);
    TEST_ASSERT_EQUAL_UINT8(1, command.commandLen);
    TEST_ASSERT_EQUAL_UINT8(0x10, command.body[0]);

    gateway::QueuedCommand stored = {};
    TEST_ASSERT_TRUE(fix.commands.find(firstId, &stored));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(gateway::CommandState::Delivered), static_cast<uint8_t>(stored.state));
    TEST_ASSERT_TRUE(fix.commands.find(secondId, &stored));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(gateway::CommandState::Pending), static_cast<uint8_t>(stored.state));
}

void test_duplicate_telemetry_is_acked_without_a_second_uplink(void) {
    Fixture fix;
    uint32_t commandId = 0;
    setStatus(fix.commands.enqueue(1, 0, nullptr, 0, &commandId), proto::Status::Ok);
    queueTelemetry(fix, 4, 3);
    setStatus(fix.service.poll(), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT8(1, fix.uplink.count);
    TEST_ASSERT_EQUAL_UINT16(1, fix.radio.txCount());

    queueTelemetry(fix, 4, 3);
    setStatus(fix.service.poll(), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT8(1, fix.uplink.count);
    TEST_ASSERT_EQUAL_UINT16(2, fix.radio.txCount());

    const proto::DecodedPacket ack = txPacket(fix);
    TEST_ASSERT_EQUAL_UINT32(2, ack.counter);
    bool hasCommand = false;
    proto::AckCommand command = {};
    setStatus(fix.codec.readAckPayload(ack.payload, ack.payloadLen, &hasCommand, &command), proto::Status::Ok);
    TEST_ASSERT_TRUE(hasCommand);
    TEST_ASSERT_EQUAL_UINT32(commandId, command.commandId);

    gateway::QueuedCommand stored = {};
    TEST_ASSERT_TRUE(fix.commands.find(commandId, &stored));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(gateway::CommandState::Delivered), static_cast<uint8_t>(stored.state));
    TEST_ASSERT_FALSE(fix.commands.nextFor(1, nullptr));
}

void test_command_result_completes_the_command_and_is_uplinked(void) {
    Fixture fix;
    uint32_t commandId = 0;
    setStatus(fix.commands.enqueue(1, 0, nullptr, 0, &commandId), proto::Status::Ok);
    queueTelemetry(fix, 1, 1);
    setStatus(fix.service.poll(), proto::Status::Ok);

    queueResult(fix, 2, 2, commandId, proto::CommandResultStatus::Ok);
    setStatus(fix.service.poll(), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT16(2, fix.radio.txCount());
    TEST_ASSERT_EQUAL_UINT8(2, fix.uplink.count);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(gateway::UplinkKind::CommandResult),
                            static_cast<uint8_t>(fix.uplink.records[1].kind));
    TEST_ASSERT_EQUAL_UINT32(commandId, fix.uplink.records[1].commandId);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(proto::CommandResultStatus::Ok), fix.uplink.records[1].resultStatus);

    const proto::DecodedPacket ack = txPacket(fix);
    TEST_ASSERT_EQUAL_UINT8(0, ack.payloadLen);
    TEST_ASSERT_EQUAL_UINT32(2, ack.counter);

    gateway::QueuedCommand stored = {};
    TEST_ASSERT_TRUE(fix.commands.find(commandId, &stored));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(gateway::CommandState::Succeeded), static_cast<uint8_t>(stored.state));

    queueResult(fix, 2, 2, commandId, proto::CommandResultStatus::Ok);
    setStatus(fix.service.poll(), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT8(2, fix.uplink.count);
    TEST_ASSERT_EQUAL_UINT16(3, fix.radio.txCount());
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_energy_body_is_stored_and_an_empty_body_is_still_acked);
    RUN_TEST(test_telemetry_gets_an_empty_ack_and_one_uplink);
    RUN_TEST(test_pending_command_is_delivered_in_the_ack);
    RUN_TEST(test_duplicate_telemetry_is_acked_without_a_second_uplink);
    RUN_TEST(test_command_result_completes_the_command_and_is_uplinked);
    return UNITY_END();
}
