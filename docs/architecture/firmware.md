# Firmware layout

The protocol does not include Arduino, SPI, Wi-Fi, or MQTT types. Node and gateway each own a thin radio service that adapts RadioLib to the codec.

PlatformIO, Arduino-ESP32, and RadioLib are the prototype stack. See [hardware.md](hardware.md) for the board bring-up this tree already proves.

```text
shared/
  protocol/
    packet_codec        encode/decode the draft header and payloads
    message_types       type ids, command ids, status codes
    security            nonce, AES-CCM, auth on/off for lab builds
    sequence_manager    seq, retry budget, duplicate detection

firmware/
  node/
    radio_service       RadioLib IRQ, TX, short RX window
    telemetry_service   interval, jitter, build Telemetry
    command_service     validate, run once, emit CommandResult
    provisioning        device id and key storage (interface first)

  gateway/
    radio_service       RX, immediate ACK, result RX
    node_registry       id, last counter, last seq, last seen, RSSI/SNR
    command_queue       persistent pending / in-flight / done
    mqtt_service        uplink telemetry and results, downlink commands
    wifi_manager        connect, backoff, link state
    storage             counters, queue, outbound buffer
```

## Boundaries

| Module | May do | Must not do |
| --- | --- | --- |
| `packet_codec` | Bytes in, struct out, and the reverse | Touch the radio or the network |
| `security` | Tag, verify, advance the counter policy | Decide product command behavior |
| `radio_service` | `startTransmit` / `startReceive`, surface RSSI/SNR, honor the half-duplex state machine | Parse JSON, publish MQTT, block on sockets |
| `command_queue` | Persist commands across reboot, hand the next command for a node id to the radio service | Transmit by itself |
| `mqtt_service` | Map backend messages onto the queue and ship buffered uplinks | Sit on the check-in path |

Shared protocol code is linked by both firmwares. It is not a third Arduino sketch.

## Node loop

1. Sleep until the reporting time (interval plus jitter).
2. Build `Telemetry`, transmit, arm the short RX window.
3. On a valid `Ack` with a command, run it once and, when required, transmit `CommandResult` and wait for that packet’s ACK.
4. On timeout, retry within the budget, then sleep until the next interval.
5. Ignore payloads that fail [validation](protocol.md#validation-order).

## Gateway loop

1. Radio events and the network client run independently.
2. On telemetry: validate, update the registry, enqueue the uplink, send `Ack` with a queued command if one exists.
3. On `CommandResult`: validate, update command state, enqueue the result uplink.
4. Network task: reconnect with backoff, flush the outbound buffer, accept new commands into the queue.

## Persistence

Minimum records:

- Per node: last accepted replay counter, last completed `command_id`.
- Gateway queue: command id, target node, type, body, state.
- Gateway outbound buffer: telemetry and results waiting for MQTT/HTTP.

Use a wear-aware approach (append or record slots, not a full rewrite on every packet) once the prototype leaves the bench. The interface is “commit this record”; the flash layout can change behind it.

## What the ping-pong sketch already shows

`src/main.cpp` is an interrupt-driven RadioLib SX1276 TX/RX loop: packet-done flag, `startTransmit`, then `startReceive`. The product radio service keeps that shape and replaces the `String` payload with the binary codec. OLED and the ping-pong role switch are demo-only.
