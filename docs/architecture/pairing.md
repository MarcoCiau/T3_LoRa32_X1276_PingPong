# Pairing and the user button

Both boards share one user button: the PRG/BOOT key on GPIO 0. See [hardware.md](hardware.md). Every gesture below is classified from how long that button stays down after the application is already running. A hold that starts during reset is the ESP32 ROM bootloader, not a gesture.

The node and gateway images on the bench today compile a node id and do not read this button. This document is the behavior to add. It does not change the F1–F4 bench, and it is not part of T0–T18.

Pairing assigns a node id. It does not create or send a key. Key provisioning stays a later document. Frames after pairing still follow [protocol.md](protocol.md), including the deployment rule that `LP_AUTH_REQUIRED` rejects a telemetry, ping, or unpair frame with no tag.

## One button, one outcome

The press is timed from the debounced falling edge. Debounce is 30 ms. Nothing runs on that edge. A press produces one outcome:

| Device | When the press ends | Outcome |
| --- | --- | --- |
| Node, unpaired | Released before 1 s | Open the node pairing window (60 s) and send `PairRequest`. |
| Node, paired | Released before 1 s | Range ping. |
| Node | Still held at 3 s | Unpair. Commit at 3 s. The release is not also a click. |
| Gateway | Released before 1 s | Open the gateway pairing window (60 s). A second click before it ends closes it. |
| Gateway | Released at or after 5 s and before 10 s | Wi-Fi setup. Commit on release. |
| Gateway | Still held at 10 s | Factory reset. Commit at 10 s, then reboot. Wi-Fi setup does not run. |

A release that falls in a gap does nothing: on the node, 1 s up to 3 s; on the gateway, 1 s up to 5 s. A slow click cannot unpair, start Wi-Fi setup, or factory-reset.

While the button is held, the OLED (if present) and the LED show the outcome that would commit. Gateway: “Wi-Fi” from 5 s, then “Reset” at 10 s. Node: “Unpair” at 3 s.

Rules that keep the gestures apart:

1. Classify by duration. A hold that reaches 3 s, 5 s, or 10 s is not a click.
2. On the gateway, Wi-Fi setup commits only if the button is released in the 5 s–10 s band. Reaching 10 s replaces that pending setup with factory reset.
3. Pairing and Wi-Fi setup are mutually exclusive. Starting one cancels the other.
4. While Wi-Fi setup is active, a click cancels setup and does not open pairing.
5. The node has no Wi-Fi setup and no factory reset. The 3 s hold is unpair only.
6. The gateway has no single-node unpair gesture. One slot is freed when that node’s `Unpair` is accepted. Every slot is freed by factory reset.
7. A hold preempts a click action already in progress on that device. A node hold during a ping abandons the ping and unpairs.

The radio IRQ does not read the button. Gesture work waits until the radio is idle, except that an ACK already started is finished before factory reset erases and reboots. One gesture may sit pending; a newer gesture replaces it.

## Pair

Both devices must be in a pairing window. Order does not matter. The windows are 60 s. The gateway does not transmit until it hears a `PairRequest`, and it answers only while its own window is open.

```text
Operator                     Node                         Gateway
  |  click (unpaired)          |                            |
  |--------------------------->|  window 60 s               |
  |  click                     |                            |
  |-------------------------------------------------------->|  window 60 s
  |                            |  PairRequest (MAC)         |
  |                            |--------------------------->|
  |                            |  PairAccept (id, MAC,      |
  |                            |   last command id)         |
  |                            |<---------------------------|
  |                            |  store id, close window    |  store binding, close window
```

1. The node’s hardware identity is the 6-byte Wi-Fi station MAC. It is stable across reboot and is not a secret.
2. `PairRequest` uses `src_id` `0xFF` (unassigned) and `dst_id` `0`. The payload is the MAC.
3. The gateway assigns the lowest free id in `1`–`10`. A MAC it already has keeps that same id.
4. `PairAccept` is `src_id` `0`, `dst_id` `0xFF`. The payload is `node_id` (`uint8`), the MAC, and `last_command_id` (`uint32`, little-endian, `0` when none).
5. The node accepts `PairAccept` only during its window, and only when the MAC is its own. It stores `node_id` and `last_command_id` before it marks itself paired.
6. Success closes both windows. The node then uses the assigned id on every later frame.
7. A full table, with the gateway window open, gets `PairReject` (`reason` `1`). The node shows failure and closes its window. The gateway window stays open until it times out or the operator clicks again.
8. A closed gateway window is silence. The gateway does not answer.
9. The node retries `PairRequest` with the same randomized delay as other transmits, and stops when the window ends or `PairAccept` arrives.
10. An unpaired node does not send telemetry.

`PairRequest`, `PairAccept`, and `PairReject` omit the auth tag, including when `LP_AUTH_REQUIRED` is set. They are accepted only inside the button window that applies to that direction. Replay checks do not apply to them. The counter on those frames may be `0`.

## Unpair

The node commits unpair when the hold reaches 3 s.

1. If the node is paired, it sends `Unpair` (assigned `src_id`, `dst_id` `0`, payload the MAC) and waits for an empty `Ack`, up to 3 tries.
2. The node then clears its paired flag and assigned id even when no `Ack` returns.
3. A gateway that accepts `Unpair` checks that `src_id` is paired and the MAC matches. It frees the id, drops queued commands for that id, clears that id’s replay counter and last command id, and returns an empty `Ack`.
4. If the gateway never hears `Unpair`, the slot stays. The next `PairRequest` from that MAC reclaims the same id and the stored `last_command_id`, so a command already completed is not run again.
5. Unpair does not erase gateway Wi-Fi credentials and does not reboot the node.

## Range ping

A click on a paired node sends `Ping`: assigned `src_id`, `dst_id` `0`, empty payload. It shares the node’s sequence counter and replay counter with telemetry. A retry reuses the same `seq`.

1. The gateway validates it as any other frame from a paired id, updates last-seen RSSI/SNR, and answers with an empty `Ack`.
2. A queued command stays queued. The ping `Ack` does not carry it. The node ignores a command body if one is present.
3. The gateway does not enqueue an MQTT uplink for a ping.
4. The node sends one retry, using the same ACK window as a check-in, then stops. Success is an accepted empty `Ack`. Failure is the window ending.
5. The reporting interval is unchanged. A ping does not replace a telemetry check-in.
6. An unpaired click does not ping. It opens pairing.

Show the result on the OLED when the panel is present. The LED is the fallback: slow blink during a pairing window, on for 2 s after a ping `Ack`, three fast blinks after a ping timeout, off after unpair. Gateway Wi-Fi setup holds the LED on. Factory reset blinks it fast until reboot.

## Gateway Wi-Fi setup

The 5 s–10 s release starts setup. It cancels an open pairing window. Check-ins from nodes already paired continue; `PairRequest` is ignored until setup ends.

1. Credentials live in NVS. Empty NVS keeps the compile-time `kWifiSsid` and `kWifiPassword`. An empty SSID still leaves the station down.
2. Setup starts a SoftAP named `lp-gw-` plus the last two MAC bytes, and serves one form for SSID and password. Those two strings are written to NVS, then the gateway returns to station mode.
3. A click, or 5 minutes with no write, cancels setup and keeps the previous credentials.
4. Setup does not erase paired nodes, the command queue, or replay counters.

## Gateway factory reset

The hold commits at 10 s, after any ACK already on the air has finished.

Erase, then reboot:

- Paired-node table (id, MAC, replay counter, last command id)
- Command queue and outbound buffer
- Wi-Fi credentials in NVS

The flashed radio profile and the MQTT macros stay. After reboot, empty Wi-Fi NVS uses the compile-time SSID again. There is no factory-reset gesture on the node.

## Records

Gateway, one record per paired id `1`–`10`: assigned flag, 6-byte MAC, replay counter, last completed `command_id`. Absent flag means the id is free.

Node: paired flag, assigned id, last completed `command_id`. The MAC is read from the chip, not stored as the identity source.

Gateway Wi-Fi NVS: SSID and password. Factory reset deletes this record.

## Prove this when pairing is implemented

These checks are not the T18 list.

- Click both devices and confirm the node stores the id the gateway assigned. A third node’s `PairRequest` during a closed gateway window gets no answer.
- Fill all 10 ids and confirm `PairReject`.
- Click a paired node in range and confirm an empty `Ack` and no command execution. Click out of range and confirm the node reports failure after one retry.
- Hold the node to 3 s. Confirm local unpair, and that a gateway which heard `Unpair` frees the id. Pair the same node again and confirm it reclaims that id without running an old command.
- Release the gateway button between 5 s and 10 s. Confirm Wi-Fi setup starts and the node table is still there. Hold to 10 s and confirm Wi-Fi setup did not start, the node table is gone, and the gateway rebooted.
- Release the gateway button before 1 s during a check-in. Confirm the ACK still goes out, and the release opened pairing rather than setup or reset.
