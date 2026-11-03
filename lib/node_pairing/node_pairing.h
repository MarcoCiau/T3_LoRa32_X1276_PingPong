/**
 * @file node_pairing.h
 * @brief Node pairing window, range ping, and unpair on the shared radio.
 *
 * Telemetry stays in NodeCheckIn. This object runs only while a gesture is active,
 * and the caller does not poll NodeCheckIn during that time.
 */

#pragma once

#include "message_types.h"
#include "node_binding.h"

#include <cstddef>
#include <cstdint>

class ClockPort;
class RadioPort;
class RandomPort;
class RecordStorePort;

namespace proto {
class AuthPort;
class PacketCodec;
}  // namespace proto

namespace node {

class NodeCheckIn;

/**
 * @brief What finished since the previous takeEvent().
 */
enum class PairEvent : uint8_t {
    None,         ///< Nothing new.
    Paired,       ///< PairAccept stored.
    PairFailed,   ///< Window ended, or PairReject.
    PingOk,       ///< Empty Ack accepted.
    PingFail,     ///< The retry window ended with no Ack.
    Unpaired,     ///< Local pairing cleared.
};

/**
 * @brief Button-driven radio session for one node.
 *
 * PairRequest, PairAccept, and PairReject omit the auth tag. Ping and Unpair use
 * the same tag policy as telemetry and share NodeCheckIn's sequence and counter.
 * This class does not call delay().
 */
class NodePairing {
public:
    /** @brief How long a click keeps sending PairRequest, in milliseconds. */
    static constexpr uint32_t kWindowMs = 60000;
    /** @brief Unpair transmits at most this many times, then clears locally. */
    static constexpr uint8_t kUnpairTries = 3;

    /**
     * @brief Bind the radio and copy @p mac.
     *
     * Every reference must outlive this object. @p mac is proto::kMacBytes.
     * The binding is loaded from @p records.
     */
    NodePairing(RadioPort& radio,
                ClockPort& clock,
                RandomPort& rng,
                proto::PacketCodec& codec,
                proto::AuthPort& auth,
                RecordStorePort& records,
                NodeCheckIn& checkIn,
                const uint8_t* mac);

    /**
     * @brief Open the pairing window and send PairRequest.
     *
     * A node that is already paired should call beginPing() instead.
     */
    void beginPair();

    /** @brief Send Ping when paired. An unpaired call opens pairing. */
    void beginPing();

    /**
     * @brief Start unpair. A hold preempts a ping or a pairing window.
     *
     * An unpaired node clears nothing on the air and reports Unpaired.
     */
    void beginUnpair();

    /**
     * @brief Advance the active session.
     * @return Status::Ok, or the radio status when a transmit fails.
     */
    proto::Status poll();

    /** @brief True while a pair, ping, or unpair session owns the radio. */
    bool active() const;

    /** @brief True when a binding is stored. */
    bool paired() const;

    /** @brief Assigned id, or 0 when unpaired. */
    uint8_t nodeId() const;

    /**
     * @brief Take the latest completion.
     * @return The event. The following call returns None until another session ends.
     */
    PairEvent takeEvent();

    /**
     * @brief LED level for this session.
     * @param now Millisecond clock.
     * @return true when the LED should be on.
     */
    bool ledOn(uint32_t now) const;

private:
    /** @brief Which gesture owns the radio. */
    enum class Job : uint8_t { None, Pair, Ping, Unpair };

    /** @brief Wait for a packet, or for the retry delay. */
    enum class Phase : uint8_t { Rx, Delay };

    /** @brief LED pattern after a session. */
    enum class Show : uint8_t { Off, Slow, Solid, Triple };

    /** @brief Transmit the frame for the current job and arm the receiver. */
    proto::Status send(uint32_t now);

    /** @brief Apply one received buffer. @return true when the session ended. */
    bool takePacket(const uint8_t* data, size_t len);

    /** @brief The listen window ended. */
    void onTimeout(uint32_t now);

    /** @brief Store the assigned id and hand it to the check-in. */
    bool commitPair(uint8_t id, uint32_t lastCommandId);

    /** @brief Clear the binding and stop telemetry. */
    void commitUnpair();

    /** @brief End the session and latch @p event. */
    void finish(PairEvent event, Show show, uint32_t now);

    RadioPort& radio_;
    ClockPort& clock_;
    RandomPort& rng_;
    proto::PacketCodec& codec_;
    proto::AuthPort& auth_;
    RecordStorePort& records_;
    NodeCheckIn& checkIn_;
    uint8_t mac_[proto::kMacBytes] = {};
    Binding binding_{};
    Job job_ = Job::None;
    Phase phase_ = Phase::Rx;
    Show show_ = Show::Off;
    PairEvent event_ = PairEvent::None;
    uint32_t windowStart_ = 0;
    uint32_t rxStart_ = 0;
    uint32_t delayStart_ = 0;
    uint32_t delayMs_ = 0;
    uint32_t showStart_ = 0;
    uint16_t seqNum_ = 1;
    uint32_t counter_ = 0;
    uint8_t sends_ = 0;
};

}  // namespace node
