# pinmap — board bring-up tool

Temporary diagnostic firmware for working out what is actually wired to a
board, as opposed to what its documentation claims.

```bash
arduino-cli compile --fqbn "esp32:esp32:esp32c5:CDCOnBoot=cdc" tools/pinmap
arduino-cli upload -p /dev/ttyACM0 --fqbn "esp32:esp32:esp32c5:CDCOnBoot=cdc" tools/pinmap
cat /dev/ttyACM0
```

Runs in about 90 seconds and prints as it goes, so a lock-up still leaves you
with everything measured up to that point.

| Phase | What it does |
|---|---|
| 1 | Electrical census. Every pin read floating, pulled up and pulled down. A pin that will not follow the internal pull is held externally. Counts edges too, so a live signal announces itself. |
| 2 | UART sweep. Every pin as a receive line, full baud sweep on the documented one. |
| 3 | Enable-pin hunt. Asserts each candidate in turn and watches whether a receiver wakes up. |
| 4 | Stimulus and response. CASIC `$PCAS06` and u-blox `MON-VER` on the documented pair, in case a module is powered but mute. |
| 5 | I2C scan across several SDA/SCL pairs. |

## Reading phase 1

| float | pull-up | pull-down | Means |
|---|---|---|---|
| 0 | 1 | 0 | Floating — nothing attached |
| 1 | 1 | 1 | Driven high — strong external pull-up, or an active driver |
| 0 | 0 | 0 | Driven low — something is holding it down |
| 1 | 1 | 0 | Weak external pull-up, losing to the internal pull-down |
| any | — | — | Edges above ~50 mean a live signal |

## Safety

Never probes **GPIO 13/14** (USB D−/D+) or **GPIO 15–22** (SPI flash) on the
ESP32-C5. Both ranges were found the hard way; see the board map in the
repository README.

Phase 3 asserts pins with the **internal pull-up only**, roughly 45k. That is
enough for a high-impedance enable input and cannot source enough current to
fight another driver. Nothing here ever drives a pin push-pull.

Porting to another chip means revisiting both excluded ranges first — they are
not the same across the ESP32 family.
