# Protocol

Binary packets on the radio link. JSON stays on MQTT and HTTP.

This is a **draft schema**. Freeze field widths, byte order, maximum payload, and compatibility rules before two firmware versions ship to the same network. Until then, all devices in a test use one `version` and reject anything else.

## Header

Multi-byte integers are **little-endian**.

| Field | Draft width | Purpose |
| --- | --- | --- |
| `version` | `uint8` | Protocol version. Receivers drop unknown versions. |
| `type` | `uint8` | Message type below. |
| `src_id` | `uint8` | Sender. Gateway is `0`. Nodes are `1`–`10`. |
| `dst_id` | `uint8` | Destination. |
| `seq` | `uint16` | Per-sender sequence. Retries reuse the same value. |
| `counter` | `uint32` | Replay counter. Strictly increasing per device. Persisted. |
| `payload_len` | `uint8` | Payload bytes that follow. |
| `payload` | `payload_len` | Type-specific body. |
| `auth_tag` | 8 bytes when auth is on, absent when off | AES-CCM tag. The SX127x CRC does not authenticate commands. |

Header size without the tag is 11 bytes. Keep application payloads small. The first target is well under 64 bytes of PHY payload so airtime stays short at SF9–SF10.

`src_id` / `dst_id` as `uint8` matches the 10-node cap. Growing past that is a version bump, not a silent widening.

## Types

| `type` | Name | Who sends it | Payload (draft) |
| --- | --- | --- | --- |
| `1` | `Telemetry` | Node | Application sensors, plus last RSSI/SNR if useful to the backend. Exact sensor layout is application-defined and versioned inside the payload. |
| `2` | `Ack` | Gateway, and a node only when acking is required by a later type | Empty, or one embedded command (see below). |
| `3` | `CommandResult` | Node | `command_id` (`uint32`), `status` (`uint8`), optional short detail. |
| `4` | `Heartbeat` | Either | Empty or a small status blob. Optional for the prototype; telemetry already proves liveness. |

There is no standalone command datagram in the first version. A pending command rides inside `Ack`.

### Command inside an ACK

| Offset | Width | Field |
| --- | --- | --- |
| 0 | `uint32` | `command_id` |
| 4 | `uint8` | `command_type` |
| 5 | `uint8` | `command_len` |
| 6 | `command_len` | `command_body` |

`command_type` values belong to the application (`set interval`, `reboot`, `set output`, …). Unknown types produce a `CommandResult` with a failure status and are not executed.

An ACK with `payload_len == 0` means “telemetry accepted, nothing to do”.

## Validation order

Drop the packet on the first failure. Do not execute side effects from a packet that fails a later check.

1. Size and `payload_len` match the buffer.
2. `version` is supported.
3. `type` is known.
4. `dst_id` is this device (gateway accepts only `dst_id == 0` in v1).
5. Authentication tag, when auth is enabled.
6. Replay `counter` is strictly newer than the last accepted counter for `src_id`, then persist the new value.
7. Type-specific parameter checks (command length, command type, ranges).

Sequence numbers detect duplicates of the *same* application action. The replay counter detects reused or reordered packets even when `seq` wraps. Both are required.

## Security foundation

Target construction: **AES-CCM**, one key per device, nonce unique for that key for the life of the key.

- The nonce covers device id and the persistent counter so a reboot cannot reuse a nonce.
- Store the counter before acting on the packet, or store it in the same commit as the side effect. A crash between “accepted” and “stored” must fail toward rejecting a replay, not toward accepting one.
- The radio CRC only detects bit errors.
- Lab builds may compile with auth disabled. That flag is refused in any build marked for deployment. The header layout stays the same so turning auth on does not invent a second packet format.

Key provisioning (how a node learns its id and key) is a later document. The protocol assumes the key is already present and is never sent on the air.

## Versioning

- `version` changes when an old receiver would mis-parse the header or a payload it claims to understand.
- New `command_type` values may ship under the same `version` if unknown types are rejected with a result and old firmware does not need to read them.
- Gateways may speak one version to all nodes in v1. Mixed versions are a later feature; do not half-implement them.
