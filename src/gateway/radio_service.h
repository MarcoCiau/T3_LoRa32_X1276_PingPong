/**
 * @file radio_service.h
 * @brief Gateway check-in coordinator: validate, record, ACK, then return.
 */

#pragma once

#include "command_queue.h"
#include "message_types.h"
#include "packet_codec.h"
#include "pairing_table.h"

#include <cstdint>

class ClockPort;
class RadioPort;

namespace proto {
class AuthPort;
class ReplayWindow;
class SequenceManager;
}  // namespace proto

namespace gateway {

class NodeRegistry;
class OutboundBuffer;

/**
 * @brief Receive one node packet and answer with an Ack before returning.
 *
 * A new telemetry sequence updates the registry, appends one uplink, and may carry
 * the node's oldest pending command. A version-1 energy body is copied onto that
 * uplink. An empty or unknown body is still Acked, with the energy fields absent.
 * The same sequence is Acked again and does not
 * append a second uplink or deliver another command. A CommandResult updates the
 * queue and appends one result uplink. Invalid packets are not answered.
 * Telemetry, ping, and unpair are answered only for an id in the pairing table.
 * PairRequest is answered only while that table's window is open. A ping Ack is
 * empty and is not queued for MQTT.
 * poll() does not open a socket and does not call delay(). startTransmit of the Ack
 * returns while the radio is still sending. The next poll() arms the receiver
 * after that transmit finishes.
 */
class RadioService {
public:
    /**
     * @brief Bind the radio, protocol objects, and gateway state.
     *
     * Every reference must outlive this object. @p seq accepts node sequence numbers
     * and assigns the Ack sequence. The Ack replay counter starts at 1 and advances
     * in this object. It is not written to the store in this task.
     *
     * @param radio Half-duplex packet radio.
     * @param clock Millisecond clock for the registry's last-seen time.
     * @param codec Frame encoder and decoder.
     * @param seq Duplicate detection and Ack sequence numbers.
     * @param replay Incoming replay counters from nodes.
     * @param auth Tag policy. LabAuthOff sends no tag.
     * @param registry Last accepted packet per node.
     * @param commands Pending and delivered commands.
     * @param uplink Sink for accepted telemetry and results. Not a network client.
     * @param pairing Paired MAC table. Must outlive this object.
     */
    RadioService(RadioPort& radio,
                 ClockPort& clock,
                 proto::PacketCodec& codec,
                 proto::SequenceManager& seq,
                 proto::ReplayWindow& replay,
                 proto::AuthPort& auth,
                 NodeRegistry& registry,
                 CommandQueue& commands,
                 OutboundBuffer& uplink,
                 PairingTable& pairing);

    /**
     * @brief Ignore PairRequest while Wi-Fi setup is active.
     * @param ignore True to stay silent on PairRequest.
     */
    void setIgnorePairRequests(bool ignore);

    /**
     * @brief Listen, and answer one packet if one is waiting.
     * @return Status::Ok when idle or the exchange finished. The radio status when transmit fails.
     */
    proto::Status poll();

private:
    /** @brief Command included in the last Ack for one node, so a retry can repeat it. */
    struct LastAck {
        bool valid = false;           ///< True after this node was Acked.
        uint16_t seq = 0;             ///< Node sequence that produced the Ack.
        bool hasCommand = false;      ///< True when the Ack carried a command.
        QueuedCommand command = {};   ///< Command copied into that Ack.
    };

    /**
     * @brief Validate @p data and Ack it when it is a new or duplicate uplink.
     * @param rssi RSSI of the packet, in dBm.
     * @param snr SNR of the packet, in dB.
     */
    proto::Status handle(const uint8_t* data, size_t len, float rssi, float snr);

    /**
     * @brief Accept a telemetry sequence that has not been seen.
     * @param seq Node sequence already recorded by the sequence manager.
     */
    proto::Status acceptTelemetry(const proto::DecodedPacket& packet, float rssi, float snr);

    /**
     * @brief Accept a CommandResult sequence that has not been seen.
     */
    proto::Status acceptResult(const proto::DecodedPacket& packet);

    /**
     * @brief Update last-seen and send an empty Ack. No uplink and no command.
     */
    proto::Status acceptPing(const proto::DecodedPacket& packet, float rssi, float snr);

    /**
     * @brief Free the node when the MAC matches, then send an empty Ack.
     */
    proto::Status acceptUnpair(const proto::DecodedPacket& packet);

    /**
     * @brief Assign an id when the pairing window is open.
     */
    proto::Status handlePairRequest(const uint8_t* data, size_t len);

    /**
     * @brief Transmit a tagless pairing response. The sequence is 1 and the counter is 0.
     */
    proto::Status sendOpen(proto::MessageType type, uint8_t dstId, const uint8_t* payload, uint8_t payloadLen);

    /**
     * @brief Transmit an Ack to @p nodeId.
     * @param command Included when @p hasCommand is true. May be null otherwise.
     */
    proto::Status sendAck(uint8_t nodeId, bool hasCommand, const QueuedCommand* command);

    RadioPort& radio_;
    ClockPort& clock_;
    proto::PacketCodec& codec_;
    proto::SequenceManager& seq_;
    proto::ReplayWindow& replay_;
    proto::AuthPort& auth_;
    NodeRegistry& registry_;
    CommandQueue& commands_;
    OutboundBuffer& uplink_;
    PairingTable& pairing_;
    bool ignorePair_ = false;
    uint32_t txCounter_ = 1;
    LastAck lastAck_[proto::kMaxNodeId + 1] = {};
};

}  // namespace gateway
