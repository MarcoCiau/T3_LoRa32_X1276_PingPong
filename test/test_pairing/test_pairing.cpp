/**
 * @file test_pairing.cpp
 * @brief Button buckets, and a node pair, ping, and unpair on a fake radio.
 */

#include <unity.h>

#include <button_gesture.h>
#include <node_binding.h>
#include <node_check_in.h>
#include <node_pairing.h>
#include <packet_codec.h>
#include <security.h>
#include <sequence_manager.h>

#include "../fakes/fake_clock.h"
#include "../fakes/fake_command_executor.h"
#include "../fakes/fake_radio.h"
#include "../fakes/fake_random.h"
#include "../fakes/memory_record_store.h"

namespace {

const uint8_t kMac[proto::kMacBytes] = {1, 2, 3, 4, 5, 6};

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
    node::NodePairing pairing;

    Fixture()
        : seq(rng),
          replay(store),
          checkIn(radio, clock, rng, seq, codec, replay, auth, commands, store, 1, 60000),
          pairing(radio, clock, rng, codec, auth, store, checkIn, kMac) {
        rng.next = 200;
        checkIn.suspend(true);
    }
};

void setStatus(proto::Status actual, proto::Status expected) {
    TEST_ASSERT_EQUAL_INT8(static_cast<int8_t>(expected), static_cast<int8_t>(actual));
}

Gesture release(ButtonGesture& button, uint32_t start, uint32_t held) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Gesture::None), static_cast<uint8_t>(button.poll(true, start)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Gesture::None),
                            static_cast<uint8_t>(button.poll(true, start + ButtonGesture::kDebounceMs)));
    const uint32_t up = start + ButtonGesture::kDebounceMs + held;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Gesture::None), static_cast<uint8_t>(button.poll(false, up)));
    return button.poll(false, up + ButtonGesture::kDebounceMs);
}

}  // namespace

void setUp(void) {}

void tearDown(void) {}

void test_click_gap_unpair_and_gateway_holds(void) {
    ButtonGesture node(ButtonRole::Node);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Gesture::Click),
                            static_cast<uint8_t>(release(node, 0, 100)));

    ButtonGesture gap(ButtonRole::Node);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Gesture::None),
                            static_cast<uint8_t>(release(gap, 0, 1500)));

    ButtonGesture hold(ButtonRole::Node);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Gesture::None), static_cast<uint8_t>(hold.poll(true, 0)));
    const uint32_t pressed = ButtonGesture::kDebounceMs;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Gesture::None), static_cast<uint8_t>(hold.poll(true, pressed)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Gesture::Unpair),
                            static_cast<uint8_t>(hold.poll(true, pressed + ButtonGesture::kNodeUnpairMs)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Gesture::None),
                            static_cast<uint8_t>(hold.poll(false, pressed + ButtonGesture::kNodeUnpairMs)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Gesture::None),
                            static_cast<uint8_t>(hold.poll(false, pressed + ButtonGesture::kNodeUnpairMs + ButtonGesture::kDebounceMs)));

    ButtonGesture gateway(ButtonRole::Gateway);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Gesture::WifiSetup),
                            static_cast<uint8_t>(release(gateway, 0, 5000)));

    ButtonGesture reset(ButtonRole::Gateway);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Gesture::None), static_cast<uint8_t>(reset.poll(true, 0)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Gesture::None),
                            static_cast<uint8_t>(reset.poll(true, ButtonGesture::kDebounceMs)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Gesture::FactoryReset),
                            static_cast<uint8_t>(reset.poll(true, ButtonGesture::kDebounceMs + ButtonGesture::kGatewayFactoryMs)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HoldHint::Reset),
                            static_cast<uint8_t>(reset.hint(ButtonGesture::kDebounceMs + ButtonGesture::kGatewayFactoryMs)));
}

void test_pair_accept_is_stored_and_ping_needs_an_ack(void) {
    Fixture fix;
    fix.pairing.beginPair();
    setStatus(fix.pairing.poll(), proto::Status::Ok);
    proto::DecodedPacket request = {};
    setStatus(fix.codec.decode(fix.radio.txData(), fix.radio.txLen(), proto::kGatewayId, false, &request),
              proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(proto::MessageType::PairRequest), static_cast<uint8_t>(request.type));
    TEST_ASSERT_EQUAL_UINT8(proto::kUnassignedId, request.srcId);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kMac, request.payload, proto::kMacBytes);

    uint8_t payload[11] = {};
    uint8_t payloadLen = 0;
    setStatus(fix.codec.writePairAccept(4, kMac, 0, payload, sizeof(payload), &payloadLen), proto::Status::Ok);
    proto::PacketHeader header = {};
    header.type = proto::MessageType::PairAccept;
    header.srcId = proto::kGatewayId;
    header.dstId = proto::kUnassignedId;
    header.seq = 1;
    header.counter = 0;
    uint8_t packet[proto::kMaxPhyBytes] = {};
    size_t len = 0;
    setStatus(fix.codec.encode(header, payload, payloadLen, false, nullptr, packet, sizeof(packet), &len), proto::Status::Ok);
    fix.radio.queueRx(packet, len);
    setStatus(fix.pairing.poll(), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(node::PairEvent::Paired), static_cast<uint8_t>(fix.pairing.takeEvent()));
    TEST_ASSERT_EQUAL_UINT8(4, fix.pairing.nodeId());
    TEST_ASSERT_FALSE(fix.pairing.active());

    node::Binding stored = {};
    TEST_ASSERT_TRUE(node::loadBinding(fix.store, &stored));
    TEST_ASSERT_TRUE(stored.paired);
    TEST_ASSERT_EQUAL_UINT8(4, stored.nodeId);

    fix.pairing.beginPing();
    setStatus(fix.pairing.poll(), proto::Status::Ok);
    proto::DecodedPacket ping = {};
    setStatus(fix.codec.decode(fix.radio.txData(), fix.radio.txLen(), proto::kGatewayId, false, &ping), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(proto::MessageType::Ping), static_cast<uint8_t>(ping.type));
    TEST_ASSERT_EQUAL_UINT8(4, ping.srcId);
    TEST_ASSERT_EQUAL_UINT8(0, ping.payloadLen);

    fix.clock.advance(node::NodeCheckIn::kAckWindowMs);
    setStatus(fix.pairing.poll(), proto::Status::Ok);
    TEST_ASSERT_TRUE(fix.pairing.active());
    fix.clock.advance(node::NodeCheckIn::kAckWindowMs);
    setStatus(fix.pairing.poll(), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(node::PairEvent::PingFail), static_cast<uint8_t>(fix.pairing.takeEvent()));
    TEST_ASSERT_TRUE(fix.pairing.paired());
}

void test_unpair_clears_the_binding_without_an_ack(void) {
    Fixture fix;
    node::Binding paired = {};
    paired.paired = true;
    paired.nodeId = 3;
    paired.lastCommandId = 9;
    paired.hasLastCommand = true;
    TEST_ASSERT_TRUE(node::saveBinding(fix.store, paired));
    node::NodePairing again(fix.radio, fix.clock, fix.rng, fix.codec, fix.auth, fix.store, fix.checkIn, kMac);
    again.beginUnpair();
    for (uint8_t i = 0; i < node::NodePairing::kUnpairTries; ++i) {
        setStatus(again.poll(), proto::Status::Ok);
        fix.clock.advance(node::NodeCheckIn::kAckWindowMs);
    }
    setStatus(again.poll(), proto::Status::Ok);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(node::PairEvent::Unpaired), static_cast<uint8_t>(again.takeEvent()));
    TEST_ASSERT_FALSE(again.paired());
    node::Binding cleared = {};
    TEST_ASSERT_TRUE(node::loadBinding(fix.store, &cleared));
    TEST_ASSERT_FALSE(cleared.paired);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_click_gap_unpair_and_gateway_holds);
    RUN_TEST(test_pair_accept_is_stored_and_ping_needs_an_ack);
    RUN_TEST(test_unpair_clears_the_binding_without_an_ack);
    return UNITY_END();
}
