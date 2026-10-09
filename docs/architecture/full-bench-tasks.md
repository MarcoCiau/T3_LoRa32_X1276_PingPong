# Full bench tasks

These tasks add the energy telemetry the lab test in [full-bench-test.md](../full-bench-test.md) needs. T0–T18 stay as they are. Do one task, verify, stop, and do not start the next one.

Node ids are not assigned on the air. `kNodeId` in `src/node/main.cpp` is the id. The gateway already accepts any sender from 1 to 10. Do not add a join protocol.

Doxygen comments belong on every new or edited `.h` and `.cpp`. Suggest a conventional commit message. Do not commit unless asked.

## Checklist

- [x] **F1** Energy payload codec
- [x] **F2** Node mock energy telemetry
- [x] **F3** Gateway prints every node without Wi-Fi
- [x] **F4** ThingsBoard energy JSON

### F1 — Energy payload codec

**Read:** [full-bench-test.md](../full-bench-test.md) “Mock energy body”, and [protocol.md](protocol.md) telemetry payload.

**Do:** Encode and decode the 15-byte body in `lib/protocol/` under `namespace proto`. Little-endian. Payload version is `1`. A bad version or a short buffer is `BadPayload`. Do not change the radio header version. Do not include Arduino, Wi-Fi, or MQTT.

**Done when:** A native test round-trips one body and rejects a truncated buffer and a bad payload version.

**Forbidden:** Reading an ADC, opening a socket, a second tag size.

**Stop.**

### F2 — Node mock energy telemetry

**Read:** `NodeCheckIn::beginReport` and `src/node/telemetry_service.h`.

**Do:** Fill the F1 body from the node id and the telemetry sequence, using the mock formula in the bench doc. `NodeCheckIn` sends that body instead of an empty payload. `src/node/telemetry_service` owns the formula. The check-in still retries the same sequence and counter. Empty-payload behavior of the codec stays valid for older tests.

**Done when:** A host test of `NodeCheckIn` shows a 15-byte telemetry payload with the mock values for node 1, sequence 1, and `pio run -e node` links.

**Forbidden:** A meter driver, `String`, JSON on the radio, changing `kReportIntervalMs`.

**Stop.**

### F3 — Gateway prints every node without Wi-Fi

**Read:** `gateway::RadioService` telemetry accept path and `src/gateway/main.cpp`.

**Do:** Copy the parsed energy body onto the uplink record when the payload version is 1. An empty or unknown body still gets an ACK and an uplink, with the energy fields marked absent. The gateway serial line prints ids 1–10, not only node 1. With `kWifiSsid` empty, print the line and do not start Wi-Fi or MQTT. Include voltage, current, power, watt-hours, power factor, and frequency when the body is present.

**Done when:** A host test accepts a 15-byte telemetry frame, stores the energy fields, and still ACKs an empty frame. `pio run -e gateway` links.

**Forbidden:** Blocking the ACK on Wi-Fi, dropping node ids 2–10, a join handshake.

**Stop.**

### F4 — ThingsBoard energy JSON

**Read:** `formatUplinkJson` and the ThingsBoard topic macros in `src/gateway/mqtt_service.h`.

**Do:** When the uplink record has an energy body, add `voltage`, `current`, `powerW`, `energyWh`, `powerFactor`, and `frequencyHz` to the fixed JSON buffer. Units are SI floats: volts, amps, watts, kilowatt-hours, a 0–1 factor, and hertz. Keep `node`, `kind`, `seq`, `counter`, `rssi`, and `snr`. One `MqttService::poll` still publishes at most one record, and only when Wi-Fi is associated and the MQTT session is up. The serial line from F3 is still printed when the broker is up.

**Done when:** A host test of the JSON mapping, with no MQTT library, matches a document that contains the energy keys. A record without energy keys stays the current object. `pio run -e gateway` links.

**Forbidden:** Calling MQTT from the radio or the IRQ, heap JSON, a second topic for telemetry.

**Stop.**
