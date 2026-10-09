# Node hardware test

Use this after T13 is flashed on the gateway board. The node image sends one telemetry frame at boot, then listens. The bring-up ping-pong image is a different build and is not this test.

## What you need

- One LilyGO T3 LoRa32 V1.6 for the node.
- A second board for the gateway, flashed with the `gateway` environment after T13.
- A USB cable and a serial monitor at **115200** baud.

Both boards use the radio profile in `include/board_profile.h`. The lab frequency is **434.0 MHz**. Bandwidth is 125 kHz, spreading factor 9, coding rate 4/7, sync word `0x12`, power 17 dBm. The old sketch's 868 MHz constant was never programmed into the radio.

## Flash the node

From this project directory, with the node board connected:

```text
pio run -e node -t upload
pio device monitor -b 115200
```

In the PlatformIO IDE, select the `node` environment, upload, and open the serial monitor at 115200.

Reset the board if the monitor missed the boot lines.

## Lines that mean the radio transmitted

```text
[node] radio begin
[node] freq MHz 434.00
[node] bw kHz 125.00 sf 9 cr 4/7 sync 0x12 power dBm 17
[node] tx bytes 11
[node] transmission finished
[node] listening
```

`transmission finished` is printed only after the SX1276 interrupt says the packet left the radio. The on-board LED turns on at that moment. `tx bytes 11` is one empty telemetry frame: version 1, type telemetry, source node 1, destination gateway 0, sequence 1, counter 1.

A sniffer set to the same profile should see that 11-byte frame. The hex line under `tx bytes` is the exact payload.

## Check-in with the gateway

1. Flash and power the gateway first, so it is already listening.
2. Reset the node. It transmits once per reset.
3. On the node monitor, `listening` should be followed by `rx bytes` when an ACK arrives. The RSSI and SNR on that line are the node's view of the gateway.
4. Press reset on the node to send the beacon again. This image does not repeat it on a timer.

`[node] radio fail` means the SX1276 did not accept the profile. Check the antenna and that this is the `node` environment, not the ping-pong environment.

## Changing the band

Edit `include/board_profile.h`, keep both boards on the same values, and upload `node` and `gateway` again. `kRadioFrequencyMhz` is the value programmed with `setFrequency()`.
