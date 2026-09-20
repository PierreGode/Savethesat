# Hardware

## Bill of materials

| Part | Notes |
|---|---|
| Seeed XIAO ESP32-C5 | Any ESP32-C5 board works; pins are all remappable |
| SSD1306 OLED (optional) | 128x64 on I2C; absent is fine, the firmware runs headless |
| GNSS module ×1–2 | u-blox strongly preferred: M10 or M9N ideal, M8N fine. NMEA-only parts work degraded |
| GNSS antenna ×1–2 | Active patch antenna |
| USB-C power bank | Runs the board all day |

One module is enough to start. Port B simply reports as absent.

## Why u-blox specifically

The primary detection layer is `UBX-MON-RF` / `UBX-MON-HW` — the receiver's own
jamming indicator, AGC and noise measurement. Cheap non-u-blox modules
(ATGM336H / AT6558, generic MTK, most no-name breakouts) emit NMEA only and
have none of it.

Savethesat runs on them anyway, from C/N0 collapse and fix loss, with the score
renormalised over the signals actually available. But it warns later and false
alarms more, and the reference channel becomes much more important. The
trade-off is spelled out in [DETECTION.md](DETECTION.md#nmea-only-receivers-atgm336h-and-friends).

Mixing is the best of both: keep the NMEA-only module on one port and add a
u-blox on the other.

## Wiring

Defaults, all changeable in `firmware/savethesat_c5/config.h`:

| Signal | GPIO | Note |
|---|---|---|
| OLED SDA | 23 | SSD1306 at 0x3C, probed at boot |
| OLED SCL | 24 | |
| GNSS A — module TX → ESP RX | 12 | HP UART1 |
| GNSS A — module RX ← ESP TX | 11 | |
| GNSS B — module TX → ESP RX | 4 | HP UART0 |
| GNSS B — module RX ← ESP TX | 5 | |
| 3V3, GND | — | Both modules |

Both at 9600 baud, the u-blox default.

### Why these pins are free

The ESP32-C5 has three UARTs — two high-power plus one low-power — and the
serial console runs over USB-Serial-JTAG rather than a UART. That leaves both
HP UARTs entirely free for GNSS. Nothing is being shared or bit-banged.

Confirmed from the SoC capability header shipped with ESP32 Arduino core 3.3.0:

```
SOC_UART_NUM                  3   (2 high-power + 1 low-power)
SOC_USB_SERIAL_JTAG_SUPPORTED 1
SOC_I2C_NUM                   2
```

GPIO 4 and 5 are the fixed LP-UART pins, but the GPIO matrix lets HP UART0
drive them, which is what the firmware does. If you would rather keep them
free, move receiver B to any other free pins.

### The reference-channel build

To use receiver B as a shielded reference — the best defence against calling an
obstructed sky "jamming" — set `GPS_B_IS_REFERENCE` to `1` and deliberately
attenuate antenna B (inside the enclosure, under a ground plane, or through a
10–20 dB pad). See [DETECTION.md](DETECTION.md).

## What the ESP32-C5 cannot do

**It cannot host an SDR.** The C5's USB-C is USB-Serial-JTAG only — a device
port, with no OTG host controller (`SOC_USB_OTG_SUPPORTED` is absent from the
capability header). No adapter changes this. And even on a chip that can host,
an RTL-SDR streams roughly 4 MB/s of raw IQ needing an FFT, against the C5's
~320 KB of usable RAM.

Spectrum analysis therefore needs a real host. That is a Raspberry Pi base
station, and it is [v2](ROADMAP.md) — one Pi for a whole network of sensors,
never one per sensor.

## Power

The board plus two GNSS modules draws roughly 150–250 mA. Any USB power bank
runs it for a full day. There is no battery management on board; if you want
unattended deployment, add a bank with pass-through charging.

## Enclosure

Nothing published yet. Keep the GNSS antennas above the board with a ground
plane under each, and keep antenna B's shielding deliberate rather than
accidental if you are using the reference-channel build.
