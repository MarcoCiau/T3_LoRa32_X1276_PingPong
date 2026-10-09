#pragma once

#include "message_types.h"

#include <cstddef>
#include <cstdint>

namespace proto {

struct PacketHeader {
    MessageType type;
    uint8_t srcId;
    uint8_t dstId;
    uint16_t seq;
    uint32_t counter;
};

// payload and tag point into the buffer passed to decode.
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

struct AckCommand {
    uint32_t commandId;
    uint8_t commandType;
    uint8_t commandLen;
    const uint8_t* body;
};

struct CommandResultBody {
    uint32_t commandId;
    uint8_t status;
    uint8_t detailLen;
    const uint8_t* detail;
};

class PacketCodec {
public:
    Status encode(const PacketHeader& header,
                  const uint8_t* payload,
                  uint8_t payloadLen,
                  bool authOn,
                  const uint8_t* tag,
                  uint8_t* out,
                  size_t outCap,
                  size_t* outLen) const;

    Status decode(const uint8_t* buf,
                  size_t len,
                  uint8_t localId,
                  bool authOn,
                  DecodedPacket* out) const;

    Status writeAckCommand(uint32_t commandId,
                           uint8_t commandType,
                           const uint8_t* body,
                           uint8_t commandLen,
                           bool authOn,
                           uint8_t* out,
                           size_t outCap,
                           uint8_t* outLen) const;

    Status readAckPayload(const uint8_t* payload,
                          uint8_t payloadLen,
                          bool* hasCommand,
                          AckCommand* command) const;

    Status writeCommandResult(uint32_t commandId,
                              uint8_t status,
                              const uint8_t* detail,
                              uint8_t detailLen,
                              bool authOn,
                              uint8_t* out,
                              size_t outCap,
                              uint8_t* outLen) const;

    Status readCommandResult(const uint8_t* payload,
                             uint8_t payloadLen,
                             CommandResultBody* out) const;

    Status checkTelemetry(const uint8_t* data, uint8_t len, bool authOn) const;
};

}  // namespace proto
