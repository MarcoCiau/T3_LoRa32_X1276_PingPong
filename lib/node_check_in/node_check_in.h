/**
 * @file node_check_in.h
 * @brief Node telemetry check-in, command policy, and the result exchange.
 */

#pragma once

#include "message_types.h"
#include "packet_codec.h"

#include <cstdint>

class ClockPort;
class CommandExecutorPort;
class RadioPort;
class RandomPort;
class RecordStorePort;

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
 * A valid empty Ack ends the cycle. An Ack that carries a command is answered
 * with one CommandResult, then this object waits for that result's Ack.
 * Unknown command types and a repeated command id do not call the executor.
 * Silence until the listen window ends consumes one retry and sends the same
 * seq. After the retry budget, the node waits for the next report interval.
 * A packet that fails validation does not move the window.
 * This class does not call delay() or RadioLib.
 */
class NodeCheckIn {
public:
    /**
     * @brief Listen time for one gateway Ack, in milliseconds.
     *
     * Bench placeholder until airtime is measured at the chosen spreading factor.
     * The same window covers the telemetry Ack and the CommandResult Ack.
     */
    static constexpr uint32_t kAckWindowMs = 500;

    /**
     * @brief Bind the ports and protocol objects for one node.
     *
     * Every reference must outlive this object. @p rng is the same source bound to
     * @p seq. Retry delay is read from @p seq, which calls @p rng. The last completed
     * command id is loaded from @p records when a 4-byte blob is present.
     *
     * @param radio Half-duplex packet radio.
     * @param clock Millisecond clock.
     * @param rng Inclusive uniform source used for retry jitter.
     * @param seq Sequence numbers and the retry budget toward the gateway.
     * @param codec Frame encoder and decoder.
     * @param replay Incoming replay counters. The gateway's counter is checked here.
     * @param auth Tag policy. LabAuthOff sends no tag.
     * @param commands Side effects for command_type 0. Called at most once per command id.
     * @param records Last completed command id, little-endian uint32 at proto::lastCommandIdKey.
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
                CommandExecutorPort& commands,
                RecordStorePort& records,
                uint8_t nodeId,
                uint32_t intervalMs);

    /**
     * @brief Advance the check-in by one step.
     *
     * The first call sends telemetry. Later calls listen, run a command, send a
     * result, retry, or wait out the interval.
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

    /** @brief Which packet transmit() sends, and which Ack the receiver expects. */
    enum class Outbound : uint8_t {
        Telemetry,      ///< Empty telemetry. The Ack may carry a command.
        CommandResult,  ///< Result for one command. The Ack must be empty.
    };

    /** @brief What a received buffer did to the cycle. */
    enum class PacketFate : uint8_t {
        Ignore,      ///< Validation failed, or the packet is not the Ack we are waiting for.
        CycleDone,   ///< Empty Ack accepted. The report interval starts.
        ResultSent,  ///< A command was answered. Listening for the result Ack.
        Failed,      ///< Encode, store, or radio failed. poll() returns the status.
    };

    /** @brief Assign the next telemetry seq and counter, then transmit. */
    proto::Status beginReport(uint32_t now);

    /**
     * @brief Assign the next result seq and counter, then transmit.
     * @param commandId Command the result refers to.
     * @param status CommandResultStatus value stored in the payload.
     */
    proto::Status beginResult(uint32_t now, uint32_t commandId, uint8_t status);

    /** @brief Transmit the seq, counter, and outbound kind already chosen. */
    proto::Status transmit(uint32_t now);

    /**
     * @brief Apply one received buffer.
     * @param status Receives the error when the fate is Failed.
     */
    PacketFate takePacket(const uint8_t* data, size_t len, uint32_t now, proto::Status* status);

    /**
     * @brief Execute or reject @p command, then send the CommandResult.
     * @param status Receives the error when the fate is Failed.
     */
    PacketFate answerCommand(const proto::AckCommand& command, uint32_t now, proto::Status* status);

    /**
     * @brief Persist @p commandId as the last completed command.
     * @return false when the store rejects the write. The previous id stays readable.
     */
    bool rememberCommand(uint32_t commandId);

    RadioPort& radio_;
    ClockPort& clock_;
    proto::SequenceManager& seq_;
    proto::PacketCodec& codec_;
    proto::ReplayWindow& replay_;
    proto::AuthPort& auth_;
    CommandExecutorPort& commands_;
    RecordStorePort& records_;
    uint8_t nodeId_;
    uint32_t intervalMs_;
    Phase phase_ = Phase::WaitInterval;
    Outbound outbound_ = Outbound::Telemetry;
    bool started_ = false;
    bool hasLastCommand_ = false;
    uint32_t waitStarted_ = 0;
    uint32_t windowStarted_ = 0;
    uint32_t retryDelayMs_ = 0;
    uint16_t seqNum_ = 0;
    uint32_t counter_ = 0;
    uint32_t lastCommandId_ = 0;
    uint32_t resultCommandId_ = 0;
    uint8_t resultStatus_ = 0;
};

}  // namespace node
