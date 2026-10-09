/**
 * @file security.h
 * @brief Lab authenticator and the persistent replay-counter window.
 *
 * AES-CCM is a later task. This header does not send keys.
 */

#pragma once

#include "message_types.h"

#include <cstddef>
#include <cstdint>

class RecordStorePort;

namespace proto {

/**
 * @brief Authentication tag length and the sign/verify operations.
 *
 * LabAuthOff omits the tag. A later authenticator appends proto::kAuthTagBytes.
 * The radio CRC is not this tag.
 */
class AuthPort {
public:
    virtual ~AuthPort() = default;

    /** @brief Tag bytes appended to a frame. Zero means the frame has no tag. */
    virtual uint8_t tagLen() const = 0;

    /**
     * @brief Write a tag for @p frame.
     * @param frame Header and payload. Not read when tagLen() is 0.
     * @param frameLen Size of @p frame.
     * @param tag Destination. Unused when tagLen() is 0, and then it is not written.
     * @return Status::Ok when the tag policy is satisfied.
     */
    virtual Status sign(const uint8_t* frame, size_t frameLen, uint8_t* tag) const = 0;

    /**
     * @brief Check a tag for @p frame.
     * @param frame Header and payload.
     * @param frameLen Size of @p frame.
     * @param tag Tag bytes. May be null when tagLen() is 0.
     * @return Status::Ok when the tag is acceptable for this policy. Status::BadAuth when it is not.
     */
    virtual Status verify(const uint8_t* frame, size_t frameLen, const uint8_t* tag) const = 0;
};

/**
 * @brief Lab policy with authentication compiled out.
 *
 * tagLen() is 0, sign() writes no bytes, and verify() returns Status::Ok.
 * The frame layout stays the same as an authenticated build: the tag is simply absent.
 *
 * Define LP_AUTH_REQUIRED for a deployment build. That macro deletes this
 * constructor, so the lab policy cannot be constructed. Lab PlatformIO envs
 * leave the macro unset.
 */
class LabAuthOff final : public AuthPort {
public:
#if defined(LP_AUTH_REQUIRED)
    LabAuthOff() = delete;
#else
    LabAuthOff() = default;
#endif

    uint8_t tagLen() const override;
    Status sign(const uint8_t* frame, size_t frameLen, uint8_t* tag) const override;
    Status verify(const uint8_t* frame, size_t frameLen, const uint8_t* tag) const override;
};

/**
 * @brief Last accepted replay counter for each sender.
 *
 * A counter is accepted only when it is strictly greater than the stored value
 * and the new value is committed. A missing record accepts the first counter.
 * Sequence numbers are a separate check.
 */
class ReplayWindow {
public:
    /** @brief First key. Ids 0 through proto::kMaxNodeId use the next keys. */
    static constexpr uint16_t kReplayKeyBase = 0x0100;

    /**
     * @brief Bind the store that survives reboot.
     * @param store Must outlive this object.
     */
    explicit ReplayWindow(RecordStorePort& store);

    /**
     * @brief Storage key for @p srcId.
     * @param srcId Sender id in 0..proto::kMaxNodeId.
     * @return kReplayKeyBase + srcId.
     */
    static uint16_t replayKey(uint8_t srcId);

    /**
     * @brief Accept @p counter from @p srcId and persist it.
     *
     * The stored word is a little-endian uint32. A failed commit leaves the
     * previous counter in place and does not return Status::Ok.
     *
     * @param srcId Sender id in 0..proto::kMaxNodeId.
     * @param counter Replay counter from the packet.
     * @return Status::Ok, Replay when @p counter is not strictly newer,
     *         BadDestination for an id outside 0..10, or BadPayload when the store rejects the write.
     */
    Status accept(uint8_t srcId, uint32_t counter);

private:
    RecordStorePort& store_;
};

}  // namespace proto
