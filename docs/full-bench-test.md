# Full bench test

This is the lab procedure for one gateway and up to ten nodes. Each node sends the 15-byte mock energy body. The gateway still prints node 1 only, without those fields, until F3 and F4 in [full-bench-tasks.md](architecture/full-bench-tasks.md).

The gateway monitor shows sequence, counter, RSSI, and SNR. It does not print voltage or energy, and the ThingsBoard JSON does not contain those fields yet.

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

Today the monitor shows only node 1:

```text
[gateway] telemetry node 1 seq 1 counter 1 rssi ... snr ...
[gateway] ack sent
```

After the energy tasks, the same empty-SSID image prints every id from 1 to 10, including the mock energy fields, and still does not open a socket.

## Gateway with ThingsBoard

Set these, then flash the gateway again:

- `kWifiSsid` and `kWifiPassword` in `src/gateway/wifi_manager.h`
- `LP_MQTT_HOST` and `LP_MQTT_ACCESS_TOKEN` in `src/gateway/mqtt_service.h`

The port stays `1883`. The access token is the MQTT username. The password stays empty. Telemetry is published to `v1/devices/me/telemetry`. The radio loop does not wait for the association or the broker.

Today a connected gateway publishes one JSON object per accepted uplink, without energy keys:

```json
{"node":1,"kind":"telemetry","seq":1,"counter":1,"rssi":-40.0,"snr":8.0}
```

After the energy tasks, that object also carries `voltage`, `current`, `powerW`, `energyWh`, `powerFactor`, and `frequencyHz`. The serial line is still printed when Wi-Fi is up. Publishing happens only after `WifiLink` is associated and the MQTT session is up. One uplink record leaves the queue per `loop`.

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
