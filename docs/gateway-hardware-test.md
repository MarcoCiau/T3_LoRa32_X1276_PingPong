# Gateway hardware test

Flash this image before the node. The gateway listens on the lab radio profile and answers a node telemetry frame with one ACK. Wi-Fi stays down until `kWifiSsid` in `src/gateway/wifi_manager.h` is set. MQTT stays down until `LP_MQTT_HOST` and `LP_MQTT_ACCESS_TOKEN` in `src/gateway/mqtt_service.h` are set. Those macros are the ThingsBoard CE device API: port 1883, the access token is the username, telemetry is `v1/devices/me/telemetry`, and RPC commands arrive on `v1/devices/me/rpc/request/+`. The radio loop does not wait for either. The ping-pong image is a different build and is not this test.

The node steps are in [node-hardware-test.md](node-hardware-test.md).

## What you need

- Two LilyGO T3 LoRa32 V1.6 boards.
- A USB cable for each board, and a serial monitor at **115200** baud.

Both boards use `include/board_profile.h`. The lab frequency is **434.0 MHz**. Bandwidth is 125 kHz, spreading factor 9, coding rate 4/7, sync word `0x12`, power 17 dBm.

## Flash the gateway

Connect the board that will be the gateway:

```text
pio run -e gateway -t upload
pio device monitor -b 115200
```

In the PlatformIO IDE, select the `gateway` environment, upload, and open the serial monitor at 115200.

Reset the board if the monitor missed the boot lines.

## Lines that mean the gateway is listening

```text
[gateway] radio begin
[gateway] freq MHz 434.00
[gateway] bw kHz 125.00 sf 9 cr 4/7 sync 0x12 power dBm 17
[gateway] listening
```

`[gateway] radio fail` means the SX1276 did not accept the profile. `[gateway] nvs fail` means the record store did not open. Check that this is the `gateway` environment, not the ping-pong environment.

## Check-in with the node

1. Leave the gateway powered and on `[gateway] listening`.
2. Flash the other board with the `node` environment if that image is not already on it. See [node-hardware-test.md](node-hardware-test.md).
3. Reset the node. The first report leaves as soon as its loop starts. Later reports wait 60 seconds after an ACK.
4. On the gateway monitor, expect:

```text
[gateway] telemetry node 1 seq 1 counter 1 rssi ... snr ...
[gateway] ack sent
```

`ack sent` is printed only after the SX1276 interrupt says the ACK left the radio. The on-board LED turns on at that moment. The next node report prints a new sequence on this monitor. The node monitor does not print the ACK.

## Changing the band

Edit `include/board_profile.h`, keep both boards on the same values, and upload `gateway` and `node` again. `kRadioFrequencyMhz` is the value programmed with `setFrequency()`.

Wi-Fi, ThingsBoard, and the ten-node serial lines are in [full-bench-test.md](full-bench-test.md). This image prints node 1 only, and it does not print energy fields.
