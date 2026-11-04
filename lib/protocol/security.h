/**
 * @file security.h
 * @brief Authenticators and the persistent replay-counter window.
 *
 * LabAuthOff omits the tag. AesCcmAuth appends an 8-byte AES-CCM tag.
 * Neither type sends the key.
 */

#pragma once

#include "message_types.h"
#include "store_keys.h"

#include <cstddef>
#include <cstdint>

class RecordStorePort;

namespace proto {

/**
 * @brief Authentication tag length and the sign/verify operations.
 *
 * LabAuthOff omits the tag. AesCcmAuth appends proto::kAuthTagBytes.
 * The radio CRC is not this tag. verify() does not store the replay counter.
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
 * @brief Placeholder key for LP_AUTH_REQUIRED builds.
 *
 * Both ends of a deployment image use this value until provisioning replaces it.
 * It is never written into a packet.
 */
constexpr uint8_t kDeploymentKey[16] = {
    0x6c, 0x70, 0x2d, 0x64, 0x65, 0x76, 0x2d, 0x6b, 0x65, 0x79, 0x2d, 0x76, 0x31, 0x00, 0x00, 0x00,
};

/**
 * @brief AES-CCM tag over a frame. The payload stays in the clear.
 *
 * The tag is 8 bytes. The 13-byte nonce is the sender id, the little-endian
 * replay counter from the header, and eight zero bytes. Call verify() before
 * ReplayWindow::accept(); a bad tag must not store the counter.
 * The key is 16 bytes and is not copied into the tag.
 */
class AesCcmAuth final : public AuthPort {
public:
    /** @brief AES-128 key length. */
    static constexpr size_t kKeyBytes = 16;
    /** @brief CCM nonce length. Two bytes remain for the payload-length field. */
    static constexpr size_t kNonceBytes = 13;

    /**
     * @brief Copy @p key.
     * @param key Sixteen key bytes. Required. Not retained by the caller after return.
     */
    explicit AesCcmAuth(const uint8_t* key);

    uint8_t tagLen() const override;
    Status sign(const uint8_t* frame, size_t frameLen, uint8_t* tag) const override;
    Status verify(const uint8_t* frame, size_t frameLen, const uint8_t* tag) const override;

private:
    uint8_t key_[kKeyBytes] = {};
    bool keyOk_ = false;
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
    /** @brief First replay-counter key. Same value as proto::kReplayCounterKeyBase. */
    static constexpr uint16_t kReplayKeyBase = kReplayCounterKeyBase;

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

    /**
     * @brief True when @p counter is not strictly newer than the stored value.
     *
     * Does not write the store. A missing record is not stale.
     */
    bool stale(uint8_t srcId, uint32_t counter);

    /**
     * @brief Forget the stored counter for @p srcId.
     * @param srcId Sender id in 0..proto::kMaxNodeId.
     * @return Status::Ok, BadDestination, or BadPayload when the store cannot erase.
     */
    Status forget(uint8_t srcId);

private:
    RecordStorePort& store_;
};

}  // namespace proto
