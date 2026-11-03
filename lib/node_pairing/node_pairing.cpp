/**
 * @file node_pairing.cpp
 * @brief Pair, ping, and unpair sessions.
 *
 * Pairing frames are sent with no auth tag. Ping and Unpair go through the same
 * seal path as telemetry. A failed validation does not shorten the listen window.
 */

#include "node_pairing.h"

#include "clock_port.h"
#include "node_check_in.h"
#include "packet_codec.h"
#include "radio_port.h"
#include "random_port.h"
#include "security.h"
#include "sequence_manager.h"

#include <cstring>

namespace node {
namespace {

/**
 * @brief Encode one frame and append the auth tag when @p auth requires one.
 */
proto::Status seal(proto::PacketCodec& codec,
                   proto::AuthPort& auth,
                   const proto::PacketHeader& header,
                   const uint8_t* payload,
                   uint8_t payloadLen,
                   bool forceOpen,
                   uint8_t* packet,
                   size_t cap,
                   size_t* packetLen) {
    const bool authOn = !forceOpen && auth.tagLen() != 0;
    uint8_t tag[proto::kAuthTagBytes];
    const uint8_t* tagPtr = nullptr;
    if (authOn) {
        uint8_t frame[proto::kMaxPhyBytes];
        size_t frameLen = 0;
        const proto::Status bare =
            codec.encode(header, payload, payloadLen, false, nullptr, frame, sizeof(frame), &frameLen);
        if (bare != proto::Status::Ok) {
            return bare;
        }
        const proto::Status signedFrame = auth.sign(frame, frameLen, tag);
        if (signedFrame != proto::Status::Ok) {
            return signedFrame;
        }
        tagPtr = tag;
    }
    return codec.encode(header, payload, payloadLen, authOn, tagPtr, packet, cap, packetLen);
}

/** @brief True when @p a and @p b match for proto::kMacBytes. */
bool sameMac(const uint8_t* a, const uint8_t* b) {
    return std::memcmp(a, b, proto::kMacBytes) == 0;
}

}  // namespace

NodePairing::NodePairing(RadioPort& radio,
                         ClockPort& clock,
                         RandomPort& rng,
                         proto::PacketCodec& codec,
                         proto::AuthPort& auth,
                         RecordStorePort& records,
                         NodeCheckIn& checkIn,
                         const uint8_t* mac)
    : radio_(radio),
      clock_(clock),
      rng_(rng),
      codec_(codec),
      auth_(auth),
      records_(records),
      checkIn_(checkIn) {
    if (mac != nullptr) {
        std::memcpy(mac_, mac, proto::kMacBytes);
    }
    (void)loadBinding(records_, &binding_);
}

void NodePairing::beginPair() {
    job_ = Job::Pair;
    phase_ = Phase::Rx;
    sends_ = 0;
    show_ = Show::Slow;
    windowStart_ = clock_.millis();
    (void)send(windowStart_);
}

void NodePairing::beginPing() {
    if (!binding_.paired) {
        beginPair();
        return;
    }
    uint16_t seq = 0;
    uint32_t counter = 0;
    if (checkIn_.takeSend(&seq, &counter) != proto::Status::Ok) {
        finish(PairEvent::PingFail, Show::Triple, clock_.millis());
        return;
    }
    seqNum_ = seq;
    counter_ = counter;
    job_ = Job::Ping;
    phase_ = Phase::Rx;
    sends_ = 0;
    show_ = Show::Off;
    (void)send(clock_.millis());
}

void NodePairing::beginUnpair() {
    show_ = Show::Off;
    if (!binding_.paired) {
        finish(PairEvent::Unpaired, Show::Off, clock_.millis());
        return;
    }
    uint16_t seq = 0;
    uint32_t counter = 0;
    if (checkIn_.takeSend(&seq, &counter) != proto::Status::Ok) {
        commitUnpair();
        finish(PairEvent::Unpaired, Show::Off, clock_.millis());
        return;
    }
    seqNum_ = seq;
    counter_ = counter;
    job_ = Job::Unpair;
    phase_ = Phase::Rx;
    sends_ = 0;
    (void)send(clock_.millis());
}

proto::Status NodePairing::poll() {
    if (job_ == Job::None) {
        return proto::Status::Ok;
    }
    const uint32_t now = clock_.millis();
    if (phase_ == Phase::Delay) {
        if (job_ == Job::Pair && static_cast<uint32_t>(now - windowStart_) >= kWindowMs) {
            finish(PairEvent::PairFailed, Show::Triple, now);
            return proto::Status::Ok;
        }
        if (static_cast<uint32_t>(now - delayStart_) < delayMs_) {
            return proto::Status::Ok;
        }
        return send(now);
    }

    uint8_t buffer[proto::kMaxPhyBytes] = {};
    size_t length = 0;
    const proto::Status read = radio_.readPacket(buffer, sizeof(buffer), &length);
    if (read == proto::Status::Ok && takePacket(buffer, length)) {
        return proto::Status::Ok;
    }
    if (static_cast<uint32_t>(now - rxStart_) < NodeCheckIn::kAckWindowMs) {
        return proto::Status::Ok;
    }
    onTimeout(now);
    return proto::Status::Ok;
}

bool NodePairing::active() const {
    return job_ != Job::None;
}

bool NodePairing::paired() const {
    return binding_.paired;
}

uint8_t NodePairing::nodeId() const {
    return binding_.paired ? binding_.nodeId : 0;
}

PairEvent NodePairing::takeEvent() {
    const PairEvent event = event_;
    event_ = PairEvent::None;
    return event;
}

bool NodePairing::ledOn(uint32_t now) const {
    if (show_ == Show::Slow) {
        return ((now / 400u) % 2u) == 0u;
    }
    if (show_ == Show::Solid) {
        return static_cast<uint32_t>(now - showStart_) < 2000u;
    }
    if (show_ == Show::Triple) {
        const uint32_t elapsed = static_cast<uint32_t>(now - showStart_);
        if (elapsed >= 600u) {
            return false;
        }
        return ((elapsed / 100u) % 2u) == 0u;
    }
    return false;
}

proto::Status NodePairing::send(uint32_t now) {
    proto::PacketHeader header = {};
    header.seq = 1;
    header.counter = 0;
    const uint8_t* payload = nullptr;
    uint8_t payloadLen = 0;
    bool open = false;
    if (job_ == Job::Pair) {
        header.type = proto::MessageType::PairRequest;
        header.srcId = proto::kUnassignedId;
        header.dstId = proto::kGatewayId;
        payload = mac_;
        payloadLen = proto::kMacBytes;
        open = true;
    } else if (job_ == Job::Ping) {
        header.type = proto::MessageType::Ping;
        header.srcId = binding_.nodeId;
        header.dstId = proto::kGatewayId;
        header.seq = seqNum_;
        header.counter = counter_;
    } else {
        header.type = proto::MessageType::Unpair;
        header.srcId = binding_.nodeId;
        header.dstId = proto::kGatewayId;
        header.seq = seqNum_;
        header.counter = counter_;
        payload = mac_;
        payloadLen = proto::kMacBytes;
    }

    uint8_t packet[proto::kMaxPhyBytes] = {};
    size_t packetLen = 0;
    const proto::Status encoded =
        seal(codec_, auth_, header, payload, payloadLen, open, packet, sizeof(packet), &packetLen);
    if (encoded != proto::Status::Ok) {
        return encoded;
    }
    const proto::Status sent = radio_.startTransmit(packet, packetLen);
    if (sent != proto::Status::Ok) {
        return sent;
    }
    (void)radio_.startReceive();
    phase_ = Phase::Rx;
    rxStart_ = now;
    sends_ = static_cast<uint8_t>(sends_ + 1);
    return proto::Status::Ok;
}

bool NodePairing::takePacket(const uint8_t* data, size_t len) {
    proto::DecodedPacket decoded = {};
    if (job_ == Job::Pair) {
        if (codec_.decode(data, len, proto::kUnassignedId, false, &decoded) != proto::Status::Ok) {
            return false;
        }
        if (decoded.type == proto::MessageType::PairAccept) {
            uint8_t id = 0;
            uint8_t mac[proto::kMacBytes] = {};
            uint32_t last = 0;
            if (codec_.readPairAccept(decoded.payload, decoded.payloadLen, &id, mac, &last) != proto::Status::Ok) {
                return false;
            }
            if (!sameMac(mac, mac_) || id < proto::kMinNodeId || id > proto::kMaxNodeId) {
                return false;
            }
            if (!commitPair(id, last)) {
                finish(PairEvent::PairFailed, Show::Triple, clock_.millis());
                return true;
            }
            finish(PairEvent::Paired, Show::Off, clock_.millis());
            return true;
        }
        if (decoded.type == proto::MessageType::PairReject) {
            finish(PairEvent::PairFailed, Show::Triple, clock_.millis());
            return true;
        }
        return false;
    }

    const bool authOn = auth_.tagLen() != 0;
    if (codec_.decode(data, len, binding_.nodeId, authOn, &decoded) != proto::Status::Ok) {
        return false;
    }
    if (decoded.srcId != proto::kGatewayId || decoded.type != proto::MessageType::Ack) {
        return false;
    }
    const size_t frameLen = authOn ? len - auth_.tagLen() : len;
    if (auth_.verify(data, frameLen, decoded.tag) != proto::Status::Ok) {
        return false;
    }
    if (job_ == Job::Ping) {
        finish(PairEvent::PingOk, Show::Solid, clock_.millis());
        return true;
    }
    commitUnpair();
    finish(PairEvent::Unpaired, Show::Off, clock_.millis());
    return true;
}

void NodePairing::onTimeout(uint32_t now) {
    if (job_ == Job::Pair) {
        if (static_cast<uint32_t>(now - windowStart_) >= kWindowMs) {
            finish(PairEvent::PairFailed, Show::Triple, now);
            return;
        }
        delayMs_ = rng_.uniform(proto::SequenceManager::kRetryDelayMinMs, proto::SequenceManager::kRetryDelayMaxMs);
        delayStart_ = now;
        phase_ = Phase::Delay;
        return;
    }
    if (job_ == Job::Ping) {
        if (sends_ >= 2) {
            finish(PairEvent::PingFail, Show::Triple, now);
            return;
        }
        (void)send(now);
        return;
    }
    if (sends_ >= kUnpairTries) {
        commitUnpair();
        finish(PairEvent::Unpaired, Show::Off, now);
        return;
    }
    (void)send(now);
}

bool NodePairing::commitPair(uint8_t id, uint32_t lastCommandId) {
    Binding next = {};
    next.paired = true;
    next.nodeId = id;
    next.lastCommandId = lastCommandId;
    next.hasLastCommand = lastCommandId != 0;
    if (!saveBinding(records_, next)) {
        return false;
    }
    binding_ = next;
    checkIn_.adopt(id, lastCommandId, next.hasLastCommand);
    return true;
}

void NodePairing::commitUnpair() {
    Binding cleared = binding_;
    cleared.paired = false;
    cleared.hasLastCommand = false;
    (void)saveBinding(records_, cleared);
    binding_ = {};
    checkIn_.suspend(true);
}

void NodePairing::finish(PairEvent event, Show show, uint32_t now) {
    job_ = Job::None;
    phase_ = Phase::Rx;
    event_ = event;
    show_ = show;
    showStart_ = now;
}

}  // namespace node
