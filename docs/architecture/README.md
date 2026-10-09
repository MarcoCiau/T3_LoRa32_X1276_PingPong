# Firmware architecture

These notes define the product firmware for a small LoRa star: **1–10 nodes**, each reporting every **1–5 minutes**, with **bidirectional commands**.

This repository is the hardware template only. Copy the board pin map and the SX1276 init from [hardware.md](hardware.md). Do not carry the ping-pong sketch forward as the network protocol.

## Decision

RadioLib is the radio driver on both node and gateway. The application protocol, command queue, and node registry are ours.

No mesh routing, no LoRaWAN network server, and no general MAC layer. The gateway is the coordinator. Commands wait on the gateway until the node checks in.

## How to read this

| Document | What it freezes |
| --- | --- |
| [system.md](system.md) | Topology, check-in transaction, failure isolation |
| [protocol.md](protocol.md) | Message types, draft header, reliability and security rules |
| [firmware.md](firmware.md) | Module boundaries for node and gateway |
| [hardware.md](hardware.md) | Pins and radio init taken from this PlatformIO project |
| [agent-development.md](agent-development.md) | C++ and dependency-injection rules, and the one-task-at-a-time build checklist |

## Scope

**Build for the first prototype**

- Star topology, gateway coordinates the radio.
- Binary packets on the air; JSON only on MQTT/HTTP.
- Telemetry check-in, immediate ACK, optional command inside that ACK.
- Separate command-result message. An ACK means the packet arrived, not that the command ran.
- Sequence numbers, bounded retries, duplicate suppression.
- Persistent command IDs and replay counters so a reboot does not drop or repeat work.
- LoRa keeps running when Wi-Fi or MQTT is down.

**Define now, implement as the codec grows**

- Exact field widths, endianness, maximum payload, and version rules. The header in [protocol.md](protocol.md) is a draft.
- AES-CCM with a unique key per device, a nonce that is never reused with that key, and a persistent replay counter.
- Provisioning of device identity and keys.

**Leave out until a test shows they are needed**

- Gateway-assigned time slots. Start with fixed radio settings and randomized reporting jitter.
- Unsolicited gateway transmits. The node asks by checking in.
- A different framework. PlatformIO with Arduino-ESP32 and RadioLib is enough for the prototype. ESP-IDF is a later option if task scheduling, watchdogs, storage, or reconnect behavior need tighter control.

## Stack

```text
MQTT broker / HTTP API
        |
        |  Wi-Fi, persistent outbound queue
        v
ESP32 gateway
  radio coordinator, command queue, node registry
        |
        |  SX1276, half duplex, RadioLib
        v
Node 01 .. Node 10
  telemetry + commands
```
