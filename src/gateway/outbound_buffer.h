/**
 * @file outbound_buffer.h
 * @brief Uplink records waiting for the network. The radio path only appends.
 */

#pragma once

#include "message_types.h"

#include <cstdint>

namespace gateway {

/**
 * @brief What an uplink record carries.
 *
 * The numeric values are stable for the host double and the later buffer.
 */
enum class UplinkKind : uint8_t {
    Telemetry = 1,      ///< A node telemetry packet the gateway accepted.
    CommandResult = 2,  ///< A node CommandResult the gateway accepted.
};

/**
 * @brief One accepted uplink, copied by value.
 *
 * Telemetry uses seq, counter, rssi, and snr. A command result uses commandId and resultStatus.
 * The other fields are zero. This struct does not own heap memory.
 */
struct UplinkRecord {
    UplinkKind kind;       ///< Telemetry or CommandResult.
    uint8_t nodeId;        ///< Sender, 1..proto::kMaxNodeId.
    uint16_t seq;          ///< Telemetry sequence. Zero for a command result.
    uint32_t counter;      ///< Telemetry replay counter. Zero for a command result.
    uint32_t commandId;    ///< Result command id. Zero for telemetry.
    uint8_t resultStatus;  ///< CommandResultStatus value. Zero for telemetry.
    float rssi;            ///< RSSI in dBm. Zero for a command result.
    float snr;             ///< SNR in dB. Zero for a command result.
};

/**
 * @brief Append-only sink for uplinks.
 *
 * The radio coordinator calls push() and returns. It does not publish, and it does not
 * wait on Wi-Fi. A later task adds the bounded queue and pop().
 */
class OutboundBuffer {
public:
    virtual ~OutboundBuffer() = default;

    /**
     * @brief Store one uplink.
     * @param record Accepted telemetry or command result.
     * @return Status::Ok when the record was stored. Status::Busy when the sink cannot take it.
     */
    virtual proto::Status push(const UplinkRecord& record) = 0;
};

}  // namespace gateway
