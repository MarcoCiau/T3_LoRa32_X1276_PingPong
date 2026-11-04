# Gateway hardware test

Flash this image before the node. The gateway listens on the lab radio profile and answers a node telemetry frame with one ACK. Wi-Fi stays down until a setup form is saved or `kWifiSsid` in `src/gateway/wifi_manager.h` is set. The setup access point opens `http://192.168.4.1`: home shows device info, Device config stores the network and the ThingsBoard device id (the MQTT username), and Firmware update installs a `.bin`. MQTT stays down until `LP_MQTT_HOST` is set and a device id or `LP_MQTT_ACCESS_TOKEN` is present. Those macros are the ThingsBoard CE device API: port 1883, the access token is the username, telemetry is `v1/devices/me/telemetry`, and RPC commands arrive on `v1/devices/me/rpc/request/+`. The radio loop does not wait for either. The ping-pong image is a different build and is not this test.

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
[gateway] telemetry node 1 seq 1 counter 1 rssi ... snr ... voltage 230.10 current 1.001 power 101 watt-hours 1001 power-factor 0.950 frequency 50.00
[gateway] ack sent
```

`ack sent` is printed only after the SX1276 interrupt says the ACK left the radio. The on-board LED turns on at that moment. The next node report prints a new sequence on this monitor. Ids 2 through 10 print the same kind of line when those nodes check in.

A node does not check in until it is paired. Click the gateway PRG/BOOT button. The line `[gateway] pairing` stays open for 60 seconds. Click the node button in that window. This monitor prints `[gateway] paired node N`, and the next telemetry line is that id. A second click before the window ends closes it. Release the same button between 5 and 10 seconds to start Wi-Fi setup (`lp-gw-` plus the last two MAC bytes). Hold it for 10 seconds to erase paired nodes, the command queue, and stored Wi-Fi credentials, then reboot. A click is a release before 1 second, so a slow press does not pair, start setup, or reset. The node steps are in [node-hardware-test.md](node-hardware-test.md).

## Changing the band

Edit `include/board_profile.h`, keep both boards on the same values, and upload `gateway` and `node` again. `kRadioFrequencyMhz` is the value programmed with `setFrequency()`.

Wi-Fi and ThingsBoard are in [full-bench-test.md](full-bench-test.md). This image prints nodes 1 through 10, including the energy fields, and it does not open a socket while the SSID is empty.
