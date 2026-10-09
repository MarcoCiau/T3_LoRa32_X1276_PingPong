# Full bench test

This is the lab procedure for one gateway and up to ten nodes. Each node sends the 15-byte mock energy body. The gateway prints that body for every id from 1 to 10. When Wi-Fi and MQTT are up, the same fields are published to ThingsBoard.

## What you need

- One LilyGO T3 LoRa32 V1.6 as the gateway.
- One board per node, up to ten. One board can stand in for several nodes if you change its id and flash it again.
- USB serial at **115200** baud.
- The radio profile in `include/board_profile.h` on every board. The lab frequency is **434.0 MHz**.

Flash the gateway first, then each node. The ping-pong environment is not this test.

## Provisioning

There is no join and no automatic id. A node id is compiled into that image.

| Id | Who |
| --- | --- |
| `0` | Gateway. Not a node. |
| `1`–`10` | Nodes. `proto::kMaxNodeId` is 10. |
| `11` or higher | Dropped by the gateway. |

Set the id in `src/node/main.cpp`:

```cpp
constexpr uint8_t kNodeId = 1;
```

Flash `pio run -e node -t upload` for that board. The boot line is `[node] check-in node 1 every ms 60000`. Repeat with `kNodeId` 2, 3, … for the other boards. Two images with the same id are the same node: their sequence and replay counter collide.

The gateway does not assign ids and does not keep a whitelist. The first valid telemetry from an id in 1–10 is stored in the in-memory registry. A reboot of the gateway clears that table. Replay counters and the last completed command id stay in NVS on each device.

`LP_AUTH_REQUIRED` is unset on the lab images, so frames have no auth tag and no per-node key. The placeholder `kDeploymentKey` is used only when that macro is set, and it is the same on every device. It is not sent on the air.

Reports are every **60 seconds** after an ACK, or after the retry budget. The first report leaves on the first `loop`.

## Gateway without Wi-Fi

Leave `kWifiSsid` and `kWifiPassword` empty in `src/gateway/wifi_manager.h`. Leave `LP_MQTT_HOST` and `LP_MQTT_ACCESS_TOKEN` empty in `src/gateway/mqtt_service.h`.

```text
pio run -e gateway -t upload
pio device monitor -b 115200
```

Boot ends with `[gateway] listening`. Wi-Fi is not started. MQTT is not started. Each accepted check-in still gets one ACK.

The monitor prints every accepted id from 1 to 10. Voltage is volts, current is amps, power is watts, watt-hours stays watt-hours, power factor is 0–1, and frequency is hertz:

```text
[gateway] telemetry node 1 seq 1 counter 1 rssi ... snr ... voltage 230.10 current 1.001 power 101 watt-hours 1001 power-factor 0.950 frequency 50.00
[gateway] ack sent
```

An empty or unknown body still prints the sequence line and still gets an ACK, without the energy fields. Wi-Fi and MQTT stay down while the SSID and token are empty.

## Gateway with ThingsBoard

Set these, then flash the gateway again:

- `kWifiSsid` and `kWifiPassword` in `src/gateway/wifi_manager.h`
- `LP_MQTT_HOST` and `LP_MQTT_ACCESS_TOKEN` in `src/gateway/mqtt_service.h`

The port stays `1883`. The access token is the MQTT username. The password stays empty. Telemetry is published to `v1/devices/me/telemetry`. The radio loop does not wait for the association or the broker.

A connected gateway publishes one JSON object per accepted uplink. `energyWh` is kilowatt-hours. The other energy values are volts, amps, watts, a 0–1 factor, and hertz:

```json
{"node":1,"kind":"telemetry","seq":1,"counter":1,"rssi":-40.0,"snr":8.0,"voltage":230.10,"current":1.001,"powerW":101.0,"energyWh":1.001,"powerFactor":0.950,"frequencyHz":50.00}
```

An uplink without an energy body omits those keys. The serial line is still printed when Wi-Fi is up. Publishing happens only after `WifiLink` is associated and the MQTT session is up. One uplink record leaves the queue per `loop`. The document is built in `buildThingsBoardPayload()`.

## Mock energy body

This layout is the telemetry payload on the air. Widths are little-endian. The radio header version stays 1.

| Offset | Width | Field | Unit |
| --- | --- | --- | --- |
| 0 | `uint8` | payload version | `1` |
| 1 | `uint16` | voltage | centivolts (23000 = 230.00 V) |
| 3 | `uint16` | current | milliamps |
| 5 | `uint16` | power | watts |
| 7 | `uint32` | energy | watt-hours |
| 11 | `uint16` | power factor | thousandths (950 = 0.950) |
| 13 | `uint16` | frequency | centihertz (5000 = 50.00 Hz) |

That is 15 bytes. The lab frame without an auth tag is 11 + 15 = 26 bytes. With the 8-byte tag the frame is 34 bytes. Both fit in the 64-byte PHY cap.

The mock values are deterministic: voltage `23000 + nodeId * 10` centivolts, current `1000 + seq` milliamps, power `100 + nodeId` watts, energy `nodeId * 1000 + seq` watt-hours, power factor `950`, frequency `5000` centihertz. They are not read from a meter.

## Order on the bench

1. Flash the gateway with an empty SSID. Confirm `[gateway] listening`.
2. Flash node 1. Confirm a telemetry line and `[gateway] ack sent`.
3. Flash any other ids you have boards for. Each new id gets its own line.
4. Only after the radio path is printing, set the Wi-Fi and ThingsBoard macros and flash the gateway again. Confirm the same radio lines, then the device telemetry in ThingsBoard.

`[gateway] radio fail` means the SX1276 rejected the profile. `[gateway] nvs fail` or `[node] nvs fail` means the record store did not open. `[node] radio fail` means that node did not program the radio.
