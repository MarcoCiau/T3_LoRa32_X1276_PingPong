# Agent development guide

Rules for an AI agent implementing the firmware in [README.md](README.md). Read the architecture doc named in the task before editing. Implement **one task**, run its checks, report, and stop.

This repository is the hardware template. `src/main.cpp` stays the ping-pong bring-up until a task tells you to change it. Product code is added beside it. Copy pins and SX1276 init from [hardware.md](hardware.md). Set frequency explicitly. `CONFIG_RADIO_FREQ` in the sketch is unused.

## Session contract

1. Find the first unchecked task in [Checklist](#checklist).
2. Read only the architecture sections that task names.
3. Change only the files that task allows.
4. Run the verification that task names. If it fails, fix that task. Do not start the next one.
5. Mark the task done and report with: task id, files changed, command output, behavior you did not implement, next task id.
6. Stop. A later task starts only when the user asks to continue.

If a requirement is missing from [protocol.md](protocol.md) or [system.md](system.md), stop and ask. Do not invent message fields, command types, or a second packet format.

## C++ for ESP32 Arduino

Toolchain: PlatformIO, Arduino-ESP32, C++17, RadioLib 7.8.1. Exceptions and RTTI stay off. Signal failure with a status code, not `throw`.

| Rule | Practice |
| --- | --- |
| Language | C++17. `#pragma once`. No `using namespace`. |
| Integers | `<cstdint>` widths. Encode and decode multi-byte fields little-endian in named functions. Do not cast a struct onto a packet buffer. |
| Errors | `enum class Status : int8_t` with `Ok = 0`. `bool` only for a single yes/no such as “record committed”. |
| Memory | Fixed buffers (`std::array` or `uint8_t buf[N]`). No `String`, no `new`/`delete`, no `std::vector` on the check-in path or in an interrupt. |
| Time | Compare `millis()` with unsigned subtraction. A blocking `delay` belongs only in the node’s wait-between-retries, and it must be bounded by the retry rule in [system.md](system.md). |
| Interrupts | The RadioLib callback sets a `volatile` flag. The callback does not allocate, log, touch flash, or call the codec, MQTT, or Wi-Fi. |
| Logging | `Serial` in adapters and `setup` only. Protocol and state machines take no logger. |
| Headers | A file under `lib/protocol/` includes only the C++ standard library and other protocol headers. `Arduino.h`, `RadioLib.h`, `WiFi.h`, and MQTT headers appear only in adapter `.cpp` files. |
| Ownership | The composition root (`setup`) creates objects with static or file duration and passes them down. Steady-state code does not allocate. |
| Hardware owners | Concrete radio, Wi-Fi, and flash adapters delete copy and move. |
| Constants | Pin numbers, radio profile, retry counts, and buffer sizes are `constexpr` in one board or config header. Magic numbers do not appear at call sites. |

Packet buffer cap: **64 bytes** PHY payload, matching the airtime budget in [protocol.md](protocol.md). Name it `kMaxPhyBytes`. Header without tag is 11 bytes. Auth tag is 8 bytes when auth is on and 0 when it is off.

Status values used by every module:

| Status | When |
| --- | --- |
| `Ok` | Parsed or accepted |
| `Truncated` | Buffer shorter than the header or `payload_len` |
| `BadVersion` | `version` is not the one this build speaks |
| `BadType` | Unknown `type` |
| `BadDestination` | `dst_id` is not this device |
| `BadAuth` | Tag check failed, or auth is required and missing |
| `Replay` | `counter` is not strictly newer |
| `Duplicate` | Same `seq` already accepted for this action |
| `BadPayload` | Type-specific length or range check failed |
| `Busy` | Radio is in the other half-duplex state |
| `Timeout` | RX window ended with no valid packet |

Command result `status` byte, draft:

| Value | Name |
| --- | --- |
| `0` | `Ok` |
| `1` | `UnknownCommand` |
| `2` | `Duplicate` |
| `3` | `Failed` |

No other command types until a task adds them. `command_type` `0` is reserved as no-op for tests.

## Inversion of control

`setup()` is the only composition root. It constructs adapters and passes them into services. Services do not call `WiFi.begin`, `radio.begin`, `Preferences`, or `new` on their collaborators.

Pass collaborators by reference into constructors. That is dependency injection. Use an abstract port only when a vendor type would otherwise leak into logic, or when tests need a second implementation.

| Collaborator | How it is injected | Abstract port |
| --- | --- | --- |
| `PacketCodec`, `SequenceManager`, outbound buffer, node registry | Concrete reference | No |
| Radio | `RadioPort` | Yes. Device: RadioLib SX1276. Test: scripted fake |
| Clock | `ClockPort` (`millis`) | Yes |
| Jitter source | `RandomPort` | Yes |
| Flash records | `RecordStorePort` | Yes. Device: NVS. Test: RAM |
| Command side effects | `CommandExecutorPort` | Yes |
| MQTT client | Concrete `MqttService` on the gateway, constructed in `setup` | No port inside the radio path |

Ports are small abstract bases: public virtual destructor, no data members, no Arduino types in the header. They live next to the module that consumes them (`lib/ports/`), not in one global interface file. Concrete adapters are `final`.

```cpp
class RadioPort {
public:
    virtual ~RadioPort() = default;
    virtual Status startTransmit(const uint8_t* data, size_t len) = 0;
    virtual Status startReceive() = 0;
    virtual Status readPacket(uint8_t* data, size_t cap, size_t* outLen) = 0;
    virtual float rssi() const = 0;
    virtual float snr() const = 0;
};

class NodeCheckIn {
public:
    NodeCheckIn(RadioPort& radio, ClockPort& clock, RandomPort& rng,
                SequenceManager& seq, PacketCodec& codec);
    Status poll();
};
```

The codec and the sequence manager stay concrete. Tests call them directly.

One process-wide binding is allowed: RadioLib’s packet callback is a C function pointer, so a single file-scope `RadioService*` in the radio adapter `.cpp` forwards the interrupt to `onIrq()`. Do not add a second global for MQTT, storage, or the codec.

Dependency direction on the gateway:

```text
RadioService --> CommandQueue, NodeRegistry, OutboundBuffer, PacketCodec
MqttService   --> OutboundBuffer, CommandQueue, WifiLink
WifiLink      --> ESP32 Wi-Fi
```

`RadioService` does not include the MQTT client. `MqttService` does not start a radio transmit.

## Layout the agent must keep

```text
lib/protocol/          codec, types, sequence, auth policy
lib/ports/             RadioPort, ClockPort, RandomPort, RecordStorePort, CommandExecutorPort
src/main.cpp           ping-pong bring-up (existing env only)
src/ui.cpp
src/node/              node adapters and setup, env:node
src/gateway/           gateway adapters and setup, env:gateway
test/fakes/            RAM and scripted doubles, native tests only
test/test_*/           Unity tests
```

PlatformIO compiles every `.cpp` under `src/` into an env unless `build_src_filter` says otherwise. The bring-up env allow-lists `main.cpp` and `ui.cpp`. `env:node` and `env:gateway` allow-list their own trees. Fakes stay under `test/` so they are not linked into device firmware.

Native tests (`env:native`, Unity) cover `lib/protocol` and state machines fed by fakes. They do not compile RadioLib.

## Checklist

Mark a box only after that task’s verification command succeeds.

- [x] **T0** Scaffold envs and folders
- [x] **T1** Message types
- [x] **T2** Header codec
- [x] **T3** Payload codec
- [x] **T4** Sequence manager
- [x] **T5** Auth port, lab auth off
- [x] **T6** Record store port and RAM fake (for port-hal, use esp32 nvs (#include <Preferences.h>) ) t
- [x] **T7** Node check-in on fakes
- [x] **T8** Node command policy
- [ ] **T9** Gateway registry and command queue
- [ ] **T10** Gateway coordinator on fakes
- [ ] **T11** Outbound buffer
- [ ] **T12** Node RadioLib adapter
- [ ] **T13** Gateway RadioLib adapter
- [ ] **T14** Wi-Fi link
- [ ] **T15** MQTT service
- [ ] **T16** Composition roots
- [ ] **T17** AES-CCM
- [ ] **T18** Bench checks

T12 and T13 need the board. T0–T11 and T17’s vectors run on the host. T14–T16 need the board for a smoke flash and still keep network code out of the codec.

### T0 — Scaffold

**Read:** [firmware.md](firmware.md), [hardware.md](hardware.md).

**Do:**

- Add `env:native` (Unity, C++17, no RadioLib).
- Add `env:node` and `env:gateway` with the same platform, board, and `lib_deps` as `env:heltec_wifi_lora_32`.
- Set `build_src_filter` so the existing env builds only `main.cpp` and `ui.cpp`.
- Create empty module files named in [firmware.md](firmware.md) under `lib/protocol/`, `lib/ports/`, `src/node/`, `src/gateway/`. Headers compile. `.cpp` files may be empty.
- Add `test/fakes/` and one native test that asserts `true` so the native env is proven.

**Done when:** `pio test -e native` passes, and `pio run -e heltec_wifi_lora_32` still passes.

**Forbidden:** Protocol behavior, radio init changes, moving `src/main.cpp`.

**Stop.**

### T1 — Message types

**Read:** [protocol.md](protocol.md) header and types.

**Do:** Add `message_types.h` with `version` constant `1`, type ids `1`–`4`, id limits (gateway `0`, nodes `1`–`10`), `kMaxPhyBytes = 64`, header size `11`, tag size `8`, command-result status codes above, and `Status`.

**Done when:** A native test fails if any constant disagrees with those values.

**Forbidden:** Encode functions, Heartbeat builders, application sensor layout.

**Stop.**

### T2 — Header codec

**Read:** [protocol.md](protocol.md) header, validation steps 1–4.

**Do:** `PacketCodec` encode and decode of the 11-byte header plus a raw payload. Reject truncated buffers, unknown version, unknown type, and `payload_len` that does not match the buffer. Auth tag bytes are copied through when the caller says auth is on, and omitted when it is off. No tag check yet.

**Done when:** Native tests round-trip a telemetry header, reject each failure above, and show little-endian `seq` and `counter` on the wire.

**Forbidden:** Radio, replay storage, command execution.

**Stop.**

### T3 — Payload codec

**Read:** [protocol.md](protocol.md) types and “Command inside an ACK”.

**Do:**

- `Ack` with `payload_len == 0`.
- `Ack` carrying one command: `command_id` `uint32`, `command_type` `uint8`, `command_len` `uint8`, body.
- `CommandResult`: `command_id`, `status`, optional detail whose length fits the remaining PHY budget.
- `Telemetry` payload is an opaque byte span, length-checked. Empty telemetry is valid.
- Reject a command whose `command_len` overruns the payload.

**Done when:** Native tests cover empty ACK, ACK with a command, result, empty telemetry, and the overrun case.

**Forbidden:** Sensor schema, command handlers, Heartbeat body.

**Stop.**

### T4 — Sequence manager

**Read:** [system.md](system.md) reliability; [protocol.md](protocol.md) on `seq` versus `counter`.

**Do:** Concrete `SequenceManager`. Next `seq` for a transmit. Retry reuses `seq`. Accepting a `seq` records it. A second accept of that `seq` returns `Duplicate`. Retry budget starts at 3. Delay is supplied by `RandomPort` and must fall in 200–800 ms inclusive. After the budget, the caller is told to wait for the next report interval.

**Done when:** Native tests use a fake RNG, check budget exhaustion, and check that a duplicate does not look like a new packet.

**Forbidden:** Replay counter (that is T5), timers that call `delay`, radio.

**Stop.**

### T5 — Auth port, lab auth off

**Read:** [protocol.md](protocol.md) security foundation and validation steps 5–6.

**Do:**

- `AuthPort` with `tagLen()`, `sign(...)`, `verify(...)`.
- `LabAuthOff` returns tag length 0, `sign` writes nothing, `verify` returns `Ok`.
- `ReplayWindow` takes `RecordStorePort`. It persists the last counter per `src_id` and accepts only a strictly greater counter. A test-local RAM stub may back the native tests; T6 replaces that stub with `MemoryRecordStore`.
- Document the build switch `LP_AUTH_REQUIRED`. When it is set, constructing `LabAuthOff` is a compile error. Leave it unset for lab envs.

**Done when:** Native tests accept counter 2 after 1, reject a replay of 2, and show a lab packet with no tag bytes.

**Forbidden:** AES-CCM implementation (T17), sending keys.

**Stop.**

### T6 — Record store

**Read:** [firmware.md](firmware.md) persistence.

**Do:** `RecordStorePort::read` and `::commit` for a `uint16` key and a fixed-size blob. `MemoryRecordStore` in `test/fakes/` keeps records in RAM. Keys needed now: replay counter per node id, last completed `command_id` per node.

**Done when:** A native test writes, rereads, and overwrites one record. Failed `commit` leaves the previous bytes readable.

**Forbidden:** NVS, wear-leveling layout, full-chip erase on boot.

**Stop.**

### T7 — Node check-in on fakes

**Read:** [system.md](system.md) check-in transaction and node loop in [firmware.md](firmware.md).

**Do:** `NodeCheckIn::poll()` against `RadioPort`, `ClockPort`, `RandomPort`, codec, sequence manager, replay window.

1. When the report interval elapses, send `Telemetry` and start receive.
2. A valid empty `Ack` ends the cycle.
3. Silence until the window ends consumes one retry and sends the same `seq`.
4. Three failures return the node to “wait for the next interval”.
5. A packet that fails validation is ignored and does not reset the window by itself.
6. RX window length is the named constant `kAckWindowMs`, starting value `500`, comment says it is a bench placeholder until airtime is measured.

`FakeRadio` records TX bytes and can queue one RX packet or none. `FakeClock` advances only when the test says so.

**Done when:** Native tests cover success, one retry then success, budget exhaustion, and a truncated ACK that does not count as success.

**Forbidden:** RadioLib, GPIO, deep sleep, MQTT, time slots.

**Stop.**

### T8 — Node command policy

**Read:** [system.md](system.md) ACK versus result; [firmware.md](firmware.md) node loop steps 3–5.

**Do:** If the ACK contains a command:

- Reject unknown `command_type` with `CommandResult` status `UnknownCommand`. Do not call the executor.
- If `command_id` equals the stored last completed id, do not call the executor. Send one `CommandResult` with status `Duplicate` so the gateway can close the command.
- Otherwise call `CommandExecutorPort::execute` once, store the id, send `CommandResult`, wait for the result’s ACK with the same retry budget.
- Executor implementation in tests is a fake that records calls. Production executor in this task is a no-op command type `0` only.

**Done when:** Tests show one executor call for a new id, zero calls for the same id again, and a result packet on the fake radio.

**Forbidden:** Real relays, reboot commands, changing the report interval from the radio.

**Stop.**

### T9 — Gateway registry and command queue

**Read:** [system.md](system.md) command lifetime; [firmware.md](firmware.md) gateway modules.

**Do:**

- `NodeRegistry`: for each id `1`–`10`, last accepted counter, last telemetry `seq`, last RSSI/SNR, last seen `millis`.
- `CommandQueue` states: pending, delivered, succeeded, failed. Persist through `RecordStorePort`. `nextFor(nodeId)` returns at most one pending command. `markDelivered` / `markResult` are explicit. Reboot is “new object, load from the store”.
- Assign `command_id` as a monotonic `uint32` stored beside the queue, unless the caller supplies the id from upstream.

**Done when:** Native tests queue two commands for one node, hand out only the first, reload from a `MemoryRecordStore` into a new queue object, and still have both commands.

**Forbidden:** Transmit, MQTT, heap-growing queues. Capacity cap: **8** commands total.

**Stop.**

### T10 — Gateway coordinator on fakes

**Read:** [system.md](system.md) gateway loop and Wi-Fi isolation.

**Do:** On a valid telemetry packet: update registry, append an uplink record to the outbound buffer interface (a test double is enough here), transmit an `Ack`. If `nextFor(src)` has a command, put it in the ACK and mark it delivered. On a valid `CommandResult`, update the queue and append a result uplink. Duplicate telemetry `seq` re-ACKs and does not append a second uplink. Radio work returns before any network call. There is no network call in this task.

**Done when:** Fake-radio tests cover telemetry to empty ACK, telemetry to ACK-with-command, duplicate telemetry, and a result that completes the command.

**Forbidden:** Wi-Fi, sockets, unsolicited gateway TX, time slots.

**Stop.**

### T11 — Outbound buffer

**Read:** [system.md](system.md) Wi-Fi and MQTT isolation.

**Do:** Concrete bounded queue, capacity **16** records. Each record is telemetry or command result. `push` and `pop`. When full, drop the oldest telemetry and increment `droppedTelemetry`. If every slot is a command result, reject the new telemetry and count the drop. Never drop a command result to store telemetry.

**Done when:** Native tests fill the buffer with mixed records and assert which ones survive.

**Forbidden:** MQTT client inside this type.

**Stop.**

### T12 — Node RadioLib adapter

**Read:** [hardware.md](hardware.md), the IRQ pattern in `src/main.cpp`.

**Do:** `RadioLibRadio` implements `RadioPort` for the node env. Copy the pin map. Call `setFrequency` with an explicit MHz constant (do not leave 434 MHz by accident if the constant says otherwise; put the chosen lab frequency in the board header and call it). Set power, bandwidth, spreading factor, coding rate, and sync word in that same header. IRQ sets a flag; `poll()` reads it. OLED stays out of this adapter.

**Done when:** `pio run -e node` succeeds. Bring-up env still succeeds. On hardware, one over-the-air transmit is visible in the RadioLib logs or a sniffer. If no board is attached, say so and leave the task unchecked.

**Forbidden:** JSON payloads, `String` packets, editing the ping-pong `loop`.

**Stop.**

### T13 — Gateway RadioLib adapter

**Read:** same as T12, plus gateway loop in [firmware.md](firmware.md).

**Do:** The same adapter type, wired to `GatewayCoordinator`. After a receive IRQ, the coordinator’s ACK transmit is the next radio operation, before any Wi-Fi call. Same board header as the node so both ends share one radio profile.

**Done when:** `pio run -e gateway` succeeds. With two boards, a node telemetry check-in produces one ACK. If only one board is available, leave the task unchecked and say what flashed.

**Forbidden:** MQTT in the receive path.

**Stop.**

### T14 — Wi-Fi link

**Read:** [system.md](system.md) Wi-Fi isolation.

**Do:** `WifiLink` in `src/gateway/`: `begin`, `poll` with reconnect backoff starting at 1 s and capping at 60 s. Expose `bool connected()`. No radio includes.

**Done when:** `pio run -e gateway` succeeds. `poll()` returns without blocking the caller for the reconnect timeout (it starts the attempt and leaves).

**Forbidden:** Publishing MQTT from this type, delay loops of unbounded length.

**Stop.**

### T15 — MQTT service

**Read:** [firmware.md](firmware.md) boundaries for `mqtt_service`.

**Do:** `MqttService::poll()` pops at most **one** outbound record per call when Wi-Fi is up and the client is connected, and publishes JSON for that record. Downlink command messages parse into `CommandQueue` (id, node, type, body). Radio code does not call `poll()`. Topics and the broker host are constexpr config. JSON is built into a fixed buffer.

**Done when:** A host test of the JSON mapping runs without the MQTT library (pure function from record to bytes). Device build compiles. A live broker test is reported separately; absence of a broker does not undo the host test.

**Forbidden:** Calling MQTT from `RadioPort` or from the IRQ.

**Stop.**

### T16 — Composition roots

**Read:** [firmware.md](firmware.md) layout.

**Do:** `src/node/main.cpp` and `src/gateway/main.cpp` construct ports, codec, stores, and services, then `poll` in `loop()` in this order on the gateway: radio coordinator, then at most one `WifiLink::poll`, then at most one `MqttService::poll`. Node `loop` only calls `NodeCheckIn::poll`. NVS implementation of `RecordStorePort` is allowed here, behind the port.

**Done when:** Both device envs link, native protocol tests still pass, and the bring-up env still passes.

**Forbidden:** Deep sleep, OLED as a failure path, a third env that is “common firmware”.

**Stop.**

### T17 — AES-CCM

**Read:** [protocol.md](protocol.md) security foundation.

**Do:** `AesCcmAuth` implements `AuthPort` with a 16-byte key and an 8-byte tag. Nonce input includes device id and the replay counter. Verify happens in validation step 5, before the counter is stored as accepted. A lab test vector (known key, nonce, plaintext, tag) lives in the native test. Deployment builds (`LP_AUTH_REQUIRED`) use this type.

**Done when:** The vector matches and a flipped ciphertext byte returns `BadAuth`. Replay tests from T5 still pass with auth on.

**Forbidden:** A new header version, sending the key, inventing a second tag size.

**Stop.**

### T18 — Bench checks

**Read:** [system.md](system.md) “Prove this before deployment”.

**Do:** Execute that list on hardware. Record frequency, power, spreading factor, bandwidth, measured ACK window, and airtime next to the board header. Do not change protocol code to “make a test pass” without a failing native test that names the bug.

**Done when:** Each bullet in that list is marked pass or fail with a one-line observation.

**Stop.**

## Later, not a current task

Deep sleep, gateway time slots, Heartbeat, mixed protocol versions, wear-aware flash layout, ESP-IDF, and unsolicited gateway transmits stay out of T0–T18. Add a new task id only after the user asks.
