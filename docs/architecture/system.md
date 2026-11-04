# System behavior

## Roles

| Role | Radio | Network | Button |
| --- | --- | --- | --- |
| Node | Wakes on its reporting interval, sends telemetry, listens briefly for one gateway response | None | Click pairs, or pings once paired. A 3 s hold unpairs. |
| Gateway | Validates uplink, answers immediately, queues commands until the matching node checks in | Wi-Fi to MQTT or HTTP. Forwards telemetry and command results on its own schedule | Click opens pairing. A release at 5 s–10 s starts Wi-Fi setup. A 10 s hold factory-resets. |

Gateway id is `0`. Node ids are `1`–`10` for the first version. `0xFF` means a node that has no id yet. Broadcast is out of scope.

## Pairing

A node sends telemetry only after the gateway has assigned it an id. Both devices enter pairing from a button click, the gateway assigns the lowest free id, and the node stores it. A later click on that node is a range ping that expects an empty ACK. A 3 s hold on the node clears the pairing. Gateway Wi-Fi setup and factory reset use longer holds on the same button so a pairing click cannot start them. Gestures, frames, and stored records are in [pairing.md](pairing.md).

Once pairing is in the firmware, the gateway accepts telemetry, ping, and unpair only from an id it has paired. It answers `PairRequest` only while its pairing window is open. The images on the bench today still use a compiled node id; pairing is not part of that bench.

## Check-in transaction

The gateway does not poll and does not transmit a command until the node has opened a receive window.

```text
Node                         Gateway                      Backend
  |  telemetry (seq, counter)   |                            |
  |---------------------------->|                            |
  |                             | validate, store telemetry  |
  |  ACK [optional command]     |                            |
  |<----------------------------|                            |
  | execute if command present  |                            |
  |  command result (if needed) |                            |
  |---------------------------->|                            |
  |  ACK                        |                            |
  |<----------------------------|                            |
  |                             |  telemetry / result        |
  |                             |--------------------------->|
```

Rules:

1. A paired node sends telemetry when it wakes or when its interval elapses. An unpaired node does not.
2. The gateway validates the packet before accepting the telemetry.
3. The gateway answers at once with an ACK. If a command is waiting for that node, the ACK carries it.
4. The node listens only for that response, validates it, and runs the command only after validation.
5. A command that changes device state gets its own result message. A pure ACK of telemetry does not.
6. The gateway uploads telemetry and results independently of the radio exchange. A failed upload must not block the next check-in.

## ACK versus result

| Event | Meaning |
| --- | --- |
| ACK | This packet was accepted (checksum, version, identity, replay counter, auth tag when enabled) |
| Command result | The node accepted the command id and reports what it did |

Duplicate packets with the same sequence number must not run the command again. The gateway may re-ACK a duplicate so the sender can stop retrying.

## Reliability

- Each sender keeps a sequence number per destination conversation.
- Retries are bounded. Delay before a retry is randomized so two nodes do not lock step.
- Starting point, to be tuned on the bench: **3 retries**, delay **uniform between 200 ms and 800 ms**, plus the node’s normal reporting jitter (on the order of seconds, inside the 1–5 minute interval).
- After the retry budget, the node waits for the next reporting interval. It does not stay awake to hunt the gateway.
- The node’s post-transmit listen window is only long enough for one gateway ACK at the configured spreading factor and bandwidth, plus a small margin. Measure that window; do not guess it in production builds.

## Command lifetime

Each command has an id assigned by the gateway (or by the backend and stored as-is).

| State | Where it lives |
| --- | --- |
| Pending | Gateway command queue, persisted |
| Delivered | Gateway has put the command in an ACK and the node’s packet was accepted |
| Succeeded / failed | Updated from the node’s result message, then reported upstream |

A gateway reboot must reload pending and in-flight commands. Losing the queue silently is a defect. A node reboot must not re-execute a command id it has already completed; store the last completed command id.

## Wi-Fi and MQTT isolation

The radio service has its own lifetime from the uplink client.

- Radio receive, ACK, and command delivery continue while Wi-Fi or MQTT is down, and while Wi-Fi setup from [pairing.md](pairing.md) is on screen.
- Telemetry and results that cannot be uploaded sit in a bounded outbound buffer. When the buffer is full, drop the oldest telemetry first and count the drop. Do not drop an unsent command result ahead of telemetry if the buffer must choose.
- Reconnect uses backoff. Network calls must not run inside the radio interrupt or inside the check-in deadline.
- MQTT starts only after the station has an IP address. Losing that address stops the client. While the address is held, the client keeps retrying the broker.

## Collisions

With 1–10 nodes and multi-minute intervals, start with one fixed radio profile and jittered report times. Add gateway-assigned slots only after a test shows collisions or command latency that jitter does not fix.

## Prove this before deployment

- Several nodes report together without a retry storm.
- A lost ACK causes a retry, and a duplicate does not run the command twice.
- Reboot the gateway and the node while a command is pending; the command still completes once.
- Disconnect MQTT; LoRa check-ins continue, and buffered data uploads after reconnect.
- Reject duplicate, malformed, replayed, and unauthenticated commands.
- Measure range and worst-case airtime at the chosen spreading factor, bandwidth, and payload size.
- Confirm frequency, power, and duty-cycle or airtime limits for the deployment region.
