/**
 * @file packet_codec.h
 * @brief Binary encode and decode of the draft radio packet.
 */

#pragma once

#include "message_types.h"

#include <cstddef>
#include <cstdint>

namespace proto {

/**
 * @brief Fields the caller supplies when encoding.
 *
 * The codec writes proto::kVersion. It does not choose the sequence number.
 */
struct PacketHeader {
    MessageType type;  ///< MessageType value written as the type byte.
    uint8_t srcId;     ///< Sender id.
    uint8_t dstId;     ///< Destination id.
    uint16_t seq;      ///< Sequence number. Retries reuse the same value.
    uint32_t counter;  ///< Replay counter field. This codec stores it and does not check it.
};

/**
 * @brief One decoded frame.
 *
 * payload and tag point into the buffer passed to PacketCodec::decode.
 * tag is null when auth is off. payload is null when payloadLen is 0.
 */
struct DecodedPacket {
    MessageType type;
    uint8_t srcId;
    uint8_t dstId;
    uint16_t seq;
    uint32_t counter;
    uint8_t payloadLen;
    const uint8_t* payload;
    const uint8_t* tag;
};

/**
 * @brief One command carried in an Ack payload.
 *
 * body points into the payload buffer. It is null when commandLen is 0.
 * Wire layout: command_id uint32, command_type uint8, command_len uint8, body.
 */
struct AckCommand {
    uint32_t commandId;
    uint8_t commandType;
    uint8_t commandLen;
    const uint8_t* body;
};

/**
 * @brief CommandResult payload view.
 *
 * detail points into the payload buffer. It is null when detailLen is 0.
 * Wire layout: command_id uint32, status uint8, then detail bytes.
 */
struct CommandResultBody {
    uint32_t commandId;
    uint8_t status;
    uint8_t detailLen;
    const uint8_t* detail;
};

/**
 * @brief Stateless encoder and decoder for the draft frame and its payloads.
 *
 * The SX127x CRC is not an auth tag. This class copies a tag when asked and does not verify it.
 */
class PacketCodec {
public:
    /**
     * @brief Encode one frame into @p out.
     *
     * Multi-byte fields are little-endian. The frame is header, payload, then tag.
     * The tag is present only when @p authOn is true.
     *
     * @param header Addressing and sequence fields.
     * @param payload Payload bytes. May be null when @p payloadLen is 0.
     * @param payloadLen Number of payload bytes. Stored in the header.
     * @param authOn When true, @p tag must hold proto::kAuthTagBytes.
     * @param tag Tag to append. Ignored when @p authOn is false.
     * @param out Destination buffer.
     * @param outCap Capacity of @p out in bytes.
     * @param outLen Receives the encoded size on success.
     * @return Status::Ok, Truncated, BadPayload, BadAuth, or BadType.
     */
    Status encode(const PacketHeader& header,
                  const uint8_t* payload,
                  uint8_t payloadLen,
                  bool authOn,
                  const uint8_t* tag,
                  uint8_t* out,
                  size_t outCap,
                  size_t* outLen) const;

    /**
     * @brief Decode one frame addressed to @p localId.
     *
     * Drops the packet on the first failed check: size, version, type, then destination.
     * A matching auth tag is copied out and not verified.
     *
     * @param buf Frame bytes.
     * @param len Size of @p buf.
     * @param localId This device. The gateway passes proto::kGatewayId.
     * @param authOn When true, the frame must end with proto::kAuthTagBytes.
     * @param out Filled only on success. Pointers alias @p buf.
     * @return Status::Ok, Truncated, BadPayload, BadVersion, BadType, or BadDestination.
     */
    Status decode(const uint8_t* buf,
                  size_t len,
                  uint8_t localId,
                  bool authOn,
                  DecodedPacket* out) const;

    /**
     * @brief Write one Ack command payload.
     *
     * @param commandId Command id, little-endian on the wire.
     * @param commandType Application command type.
     * @param body Command body. May be null when @p commandLen is 0.
     * @param commandLen Body length.
     * @param authOn Selects the payload budget inside proto::kMaxPhyBytes.
     * @param out Destination payload buffer.
     * @param outCap Capacity of @p out.
     * @param outLen Receives the payload size on success.
     * @return Status::Ok, Truncated, or BadPayload when the command does not fit.
     */
    Status writeAckCommand(uint32_t commandId,
                           uint8_t commandType,
                           const uint8_t* body,
                           uint8_t commandLen,
                           bool authOn,
                           uint8_t* out,
                           size_t outCap,
                           uint8_t* outLen) const;

    /**
     * @brief Read an Ack payload.
     *
     * payloadLen 0 means the Ack carries no command. A non-empty payload must be
     * exactly one command; a command_len that overruns or undershoots is rejected.
     *
     * @param payload Ack payload. May be null when @p payloadLen is 0.
     * @param payloadLen Length of @p payload.
     * @param hasCommand Set false for an empty Ack and true when @p command is filled.
     * @param command Filled only when a command is present. May be null for an empty Ack.
     * @return Status::Ok or BadPayload.
     */
    Status readAckPayload(const uint8_t* payload,
                          uint8_t payloadLen,
                          bool* hasCommand,
                          AckCommand* command) const;

    /**
     * @brief Write a CommandResult payload.
     *
     * @param commandId Command the result refers to.
     * @param status CommandResultStatus value, stored as a raw byte.
     * @param detail Optional detail. May be null when @p detailLen is 0.
     * @param detailLen Detail length. The prefix plus detail must fit the PHY budget.
     * @param authOn Selects the payload budget inside proto::kMaxPhyBytes.
     * @param out Destination payload buffer.
     * @param outCap Capacity of @p out.
     * @param outLen Receives the payload size on success.
     * @return Status::Ok, Truncated, or BadPayload.
     */
    Status writeCommandResult(uint32_t commandId,
                              uint8_t status,
                              const uint8_t* detail,
                              uint8_t detailLen,
                              bool authOn,
                              uint8_t* out,
                              size_t outCap,
                              uint8_t* outLen) const;

    /**
     * @brief Read a CommandResult payload.
     *
     * @param payload Result payload. Must not be null.
     * @param payloadLen Length of @p payload. At least 5 bytes.
     * @param out Filled on success. detail aliases @p payload.
     * @return Status::Ok or BadPayload.
     */
    Status readCommandResult(const uint8_t* payload,
                             uint8_t payloadLen,
                             CommandResultBody* out) const;

    /**
     * @brief Accept a telemetry payload as an opaque span.
     *
     * Empty telemetry is valid. The bytes are not parsed.
     *
     * @param data Payload bytes. May be null when @p len is 0.
     * @param len Payload length.
     * @param authOn Selects the payload budget inside proto::kMaxPhyBytes.
     * @return Status::Ok, or BadPayload when @p len exceeds the budget.
     */
    Status checkTelemetry(const uint8_t* data, uint8_t len, bool authOn) const;
};

}  // namespace proto
