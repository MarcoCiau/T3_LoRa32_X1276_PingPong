/**
 * @file sequence_manager.h
 * @brief Per-peer sequence numbers, duplicate detection, and the retry budget.
 */

#pragma once

#include "message_types.h"

#include <cstdint>

class RandomPort;

namespace proto {

/**
 * @brief What the sender should do after a transmit times out.
 */
enum class RetryResult : uint8_t {
    Again = 0,             ///< Retransmit the same seq after delayMs.
    WaitForInterval = 1,   ///< Retry budget is spent. Wait for the next report.
};

/**
 * @brief Sequence state for one sender and one receiver.
 *
 * A seq detects a repeated action. The replay counter is a separate later check.
 * Retry delays come from RandomPort. This class does not call delay() or touch the radio.
 */
class SequenceManager {
public:
    /** @brief Retransmits allowed after the original send. */
    static constexpr uint8_t kRetryBudget = 3;
    /** @brief Inclusive lower bound of the retry delay, in milliseconds. */
    static constexpr uint32_t kRetryDelayMinMs = 200;
    /** @brief Inclusive upper bound of the retry delay, in milliseconds. */
    static constexpr uint32_t kRetryDelayMaxMs = 800;

    /**
     * @brief Bind the jitter source used for retry delays.
     * @param rng Inclusive uniform source. Must outlive this object.
     */
    explicit SequenceManager(RandomPort& rng);

    /**
     * @brief Start a transmit to @p dstId and assign the next seq.
     *
     * The first seq for a destination is 1. A later call advances seq and reloads the retry budget.
     * Each destination has its own sequence.
     *
     * @param dstId Peer id, proto::kGatewayId or a node id through proto::kMaxNodeId.
     * @param seq Receives the seq to put on the packet.
     * @return Status::Ok, BadPayload when @p seq is null, or BadDestination for an id outside 0..10.
     */
    Status beginTransmit(uint8_t dstId, uint16_t* seq);

    /**
     * @brief Record that the current transmit to @p dstId got no valid response.
     *
     * While retries remain, @p result is Again, @p seq is unchanged, and @p delayMs is
     * RandomPort::uniform(kRetryDelayMinMs, kRetryDelayMaxMs). The next call after the
     * budget is spent sets WaitForInterval and does not ask for another delay.
     *
     * @param dstId Destination passed to beginTransmit.
     * @param result Receives Again or WaitForInterval.
     * @param seq Receives the seq to reuse.
     * @param delayMs Receives the retry delay, or 0 when the budget is spent.
     * @return Status::Ok, BadPayload when no transmit is in flight or a pointer is null,
     *         or BadDestination for an id outside 0..10.
     */
    Status onTimeout(uint8_t dstId, RetryResult* result, uint16_t* seq, uint32_t* delayMs);

    /**
     * @brief Record @p seq from @p srcId.
     *
     * The same seq as the last accepted value returns Status::Duplicate and is not a new packet.
     * A different seq is accepted. Peers do not share this state.
     *
     * @param srcId Sender id, in 0..10.
     * @param seq Sequence number from the packet.
     * @return Status::Ok, Duplicate, or BadDestination.
     */
    Status accept(uint8_t srcId, uint16_t seq);

    /**
     * @brief Undo the last successful accept() from @p srcId.
     *
     * Used when a later check rejects the packet. A duplicate accept is left as it was.
     */
    void revert(uint8_t srcId);

private:
    /** @brief In-flight transmit for one destination. */
    struct Tx {
        uint16_t seq;        ///< Last seq assigned to this destination. 0 before the first begin.
        uint8_t retriesLeft; ///< Remaining retransmits. Reloaded to kRetryBudget on begin.
        bool active;         ///< True after begin until the budget is spent.
    };

    /** @brief Last accepted seq from one sender. */
    struct Rx {
        uint16_t seq;       ///< Last accepted seq. Meaningful when seen is true.
        uint16_t previous;  ///< Seq before the last successful accept.
        bool seen;          ///< False until the first accept from this sender.
        bool had;           ///< True when previous is a real earlier seq.
        bool fresh;         ///< True when the last accept() stored a new seq.
    };

    /** @brief One slot per id from 0 through proto::kMaxNodeId. */
    static constexpr uint8_t kSlots = kMaxNodeId + 1;

    /** @brief Transmit slot for @p id, or null when @p id is outside 0..10. */
    Tx* txSlot(uint8_t id);
    /** @brief Receive slot for @p id, or null when @p id is outside 0..10. */
    Rx* rxSlot(uint8_t id);

    RandomPort& rng_;
    Tx tx_[kSlots] = {};
    Rx rx_[kSlots] = {};
};

}  // namespace proto
