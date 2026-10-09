# Node hardware test

Use this with the gateway image already listening. The node image checks in on a 60 second interval: one empty telemetry frame, then a short listen for the ACK. The bring-up ping-pong image is a different build and is not this test.

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

## Lines that mean the node is ready

```text
[node] radio begin
[node] freq MHz 434.00
[node] bw kHz 125.00 sf 9 cr 4/7 sync 0x12 power dBm 17
[node] check-in node 1 every ms 60000
```

The first report leaves on the first `loop`. There is no per-packet line on the node monitor. The frame is telemetry: version 1, type telemetry, source node 1, destination gateway 0, plus the 15-byte mock energy body. The first sequence is 1 and the first counter is 1. A lab frame is 26 bytes. After an ACK, or after the retry budget, the next report waits 60 seconds.

`[node] radio fail` means the SX1276 did not accept the profile. `[node] nvs fail` means the record store did not open. Check the antenna and that this is the `node` environment, not the ping-pong environment.

## Check-in with the gateway

Flash and power the gateway first. The gateway steps are in [gateway-hardware-test.md](gateway-hardware-test.md).

1. Leave the gateway on `[gateway] listening`.
2. Reset the node and wait for `[node] check-in node 1 every ms 60000`.
3. On the gateway monitor, expect a telemetry line for node 1, then `[gateway] ack sent`.
4. Leave both boards powered. The node sends again about 60 seconds after each accepted ACK. The gateway prints each new sequence.

A sniffer set to the same profile should see a 26-byte frame for each report.

## Changing the band

Edit `include/board_profile.h`, keep both boards on the same values, and upload `node` and `gateway` again. `kRadioFrequencyMhz` is the value programmed with `setFrequency()`.

The multi-node procedure, including how an id is chosen, is in [full-bench-test.md](full-bench-test.md). This image sends the mock energy body. The gateway prints those fields for nodes 1 through 10.
