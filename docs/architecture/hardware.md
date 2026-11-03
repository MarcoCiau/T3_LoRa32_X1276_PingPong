# Hardware template

Copy this pin map and radio init into the new firmware. The ping-pong application in `src/main.cpp` is not part of the template.

The comments in this tree identify the board as a **LilyGO T3 LoRa32 V1.6** (SX1276, SSD1306 128×64). `platformio.ini` uses the PlatformIO board id `heltec_wifi_lora_32` because that id matches the ESP32 module and flash layout used for upload. **The pin numbers in source are the ones to copy**, not Heltec’s default pins.

## RadioLib module

Library: `jgromes/RadioLib @ 7.8.1` (see `platformio.ini`).

```cpp
SPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN);
SX1276 radio = new Module(RADIO_CS_PIN, RADIO_DIO0_PIN, RADIO_RST_PIN, RADIO_DIO1_PIN);
int state = radio.begin();
radio.setOutputPower(CONFIG_RADIO_OUTPUT_POWER);
radio.setPacketReceivedAction(onRadioIrq);
```

`DIO2` is defined on the board and is not passed into `Module`.

## Pins

| Signal | GPIO | Used by the template |
| --- | --- | --- |
| I2C SDA | 21 | OLED |
| I2C SCL | 22 | OLED |
| Radio SCLK | 5 | SX1276 SPI |
| Radio MISO | 19 | SX1276 SPI |
| Radio MOSI | 27 | SX1276 SPI |
| Radio CS | 18 | SX1276 |
| Radio RST | 23 | SX1276 |
| Radio DIO0 | 26 | TX/RX done IRQ |
| Radio DIO1 | 33 | Passed to RadioLib as the fourth IRQ pin |
| Radio DIO2 | 32 | Defined, unused by `Module` |
| LED | 25 | Active high. Gesture feedback when the OLED is missing. |
| User button | 0 | PRG/BOOT, active low, internal pull-up. The only gesture input. See [pairing.md](pairing.md). |
| ADC | 35 | Battery / board ADC, unused by the radio loop |
| SD MOSI | 15 | Defined, unused |
| SD MISO | 2 | Defined, unused |
| SD SCLK | 14 | Defined, unused |
| SD CS | 13 | Defined, unused |

OLED: SSD1306 128×64, I2C address `0x3C`, no reset pin (`src/ui.cpp`). The display is optional for the product; the radio must boot when the panel is missing. Gesture feedback falls back to the LED.

The RST pad is a hardware reset. It is not a pairing, unpair, Wi-Fi, or factory-reset gesture. GPIO 0 held while the ESP32 leaves reset starts the ROM serial bootloader. Gesture timing in [pairing.md](pairing.md) starts only after the application is running.

## Radio profile in this tree

`radio.begin()` is called with **RadioLib defaults** (434.0 MHz, 125 kHz bandwidth, spreading factor 9, coding rate 4/7, sync word `0x12`, preamble 8). The sketch then sets output power to **17 dBm**.

`CONFIG_RADIO_FREQ` is `868.0` in `src/main.cpp` and is **never applied**. A new firmware must set frequency explicitly for the deployment region and must not assume the constant is live.

Before a field build, fix and record:

- Center frequency and regulatory band
- Transmit power
- Bandwidth, spreading factor, coding rate
- Sync word (do not keep a lab default if other networks share the channel)
- Measured airtime for the largest `Telemetry` and the largest command ACK

Duty-cycle or dwell limits of the region apply to the gateway as well as the nodes, because the gateway transmits an ACK on every accepted check-in.
