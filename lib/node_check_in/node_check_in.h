/**
 * @file node_check_in.h
 * @brief Node telemetry check-in: send, listen for an empty ACK, retry on silence.
 */

#pragma once

#include "message_types.h"

#include <cstdint>

class ClockPort;
class RadioPort;
class RandomPort;

namespace proto {
class AuthPort;
class PacketCodec;
class ReplayWindow;
class SequenceManager;
}  // namespace proto

namespace node {

/**
 * @brief One node's report cycle on a half-duplex radio.
 *
 * A valid empty Ack ends the cycle. Silence until the listen window ends consumes
 * one retry and sends the same seq. After the retry budget, the node waits for the
 * next report interval. A packet that fails validation does not move the window.
 * Command execution is a later task. This class does not call delay() or RadioLib.
 */
class NodeCheckIn {
public:
    /**
     * @brief Listen time for one gateway Ack, in milliseconds.
     *
     * Bench placeholder until airtime is measured at the chosen spreading factor.
     */
    static constexpr uint32_t kAckWindowMs = 500;

    /**
     * @brief Bind the ports and protocol objects for one node.
     *
     * Every reference must outlive this object. @p rng is the same source bound to
     * @p seq. Retry delay is read from @p seq, which calls @p rng.
     *
     * @param radio Half-duplex packet radio.
     * @param clock Millisecond clock.
     * @param rng Inclusive uniform source used for retry jitter.
     * @param seq Sequence numbers and the retry budget toward the gateway.
     * @param codec Frame encoder and decoder.
     * @param replay Incoming replay counters. The gateway's counter is checked here.
     * @param auth Tag policy. LabAuthOff sends no tag.
     * @param nodeId This node, 1..proto::kMaxNodeId.
     * @param intervalMs Time between successful cycles, or after the retry budget is spent.
     */
    NodeCheckIn(RadioPort& radio,
                ClockPort& clock,
                RandomPort& rng,
                proto::SequenceManager& seq,
                proto::PacketCodec& codec,
                proto::ReplayWindow& replay,
                proto::AuthPort& auth,
                uint8_t nodeId,
                uint32_t intervalMs);

    /**
     * @brief Advance the check-in by one step.
     *
     * The first call sends telemetry. Later calls listen, retry, or wait out the interval.
     * @return Status::Ok while the cycle is progressing, or the radio status when transmit fails.
     */
    proto::Status poll();

    /**
     * @brief True after an empty Ack, or after the retry budget, until the next report is due.
     */
    bool inIntervalWait() const;

private:
    /** @brief What poll() is waiting on. */
    enum class Phase : uint8_t {
        WaitInterval,  ///< Sleeping until the next report.
        Receive,       ///< Listening for one Ack.
        WaitRetry,     ///< Window elapsed. Waiting for the retry delay.
    };

    /** @brief Assign the next seq and counter, then transmit. */
    proto::Status beginReport(uint32_t now);

    /** @brief Transmit the seq and counter already chosen. */
    proto::Status transmit(uint32_t now);

    /**
     * @brief Accept a buffer as an empty Ack.
     * @return Status::Ok only when the cycle should end.
     */
    proto::Status acceptAck(const uint8_t* data, size_t len);

    RadioPort& radio_;
    ClockPort& clock_;
    proto::SequenceManager& seq_;
    proto::PacketCodec& codec_;
    proto::ReplayWindow& replay_;
    proto::AuthPort& auth_;
    uint8_t nodeId_;
    uint32_t intervalMs_;
    Phase phase_ = Phase::WaitInterval;
    bool started_ = false;
    uint32_t waitStarted_ = 0;
    uint32_t windowStarted_ = 0;
    uint32_t retryDelayMs_ = 0;
    uint16_t seqNum_ = 0;
    uint32_t counter_ = 0;
};

}  // namespace node
