# Savethesat

**An open GPS jamming detector that costs about as much as a takeaway.**

GNSS interference over the Baltic has stopped being an occasional curiosity and
become a standing condition. Aircraft report it, ships report it, and the
reports are largely anecdotal because almost nobody on the ground is measuring.
Savethesat is a small device that measures — and enough of them measure at once
to say *where*.

[**Flash a device from your browser →**](https://pierregode.github.io/Savethesat/)

---

## What it is

A Seeed XIAO ESP32-C5 with one or two u-blox GNSS receivers. It watches the
receivers' own interference telemetry, scores it against a baseline it learns
for itself, and serves a dashboard over its own Wi-Fi. Power it from a USB bank
and put it on a windowsill, a mast or a dashboard.

- **Detects** interference from five independent signals, and shows its working
- **Self-baselining** — no site survey, no calibration, no magic numbers
- **Phone dashboard** over its own AP, with **your mobile data still working**
- **OLED status screen** when one is fitted, headless when it is not
- **ESP-NOW mesh** so several nodes can be worked as a network
- **Browser flasher** — no toolchain, no IDE, no Arduino install
- **Receive only.** It never transmits in a GNSS band.

## Quick start

1. Flash a board from [the web flasher](https://pierregode.github.io/Savethesat/)
   (desktop Chrome or Edge — Web Serial doesn't exist on phones).
2. Wire a u-blox module to GPIO 12/11. [Wiring →](docs/HARDWARE.md)
3. Power it up, join Wi-Fi `Savethesat-XXXX` / `savethesat`.
4. Open `http://192.168.4.1/`.
5. Leave it alone for a minute while it learns what quiet looks like.

## Why your phone keeps working

The access point offers **no internet uplink and runs no captive portal**, and
this is deliberate rather than a missing feature. Your phone tests the network,
finds no uplink, keeps mobile data as its default route for everything else —
and still routes the AP's own subnet over Wi-Fi. Dashboard and Instagram at the
same time.

Any attempt to be clever here — a fake portal, an offered default route — is
precisely what would break your cellular connection.

## How it decides

| Signal | Why it matters |
|---|---|
| u-blox jamming indicator | The receiver's own interference estimate |
| C/N0 collapse | Satellites still visible, carrier-to-noise gone |
| AGC level | Falls as input power rises — something loud arrived |
| Noise per ms | Broadband noise floor at the front end |
| Fix lost, sats visible | Separates jamming from simply having no sky |

Weighted into a 0–100 score: `CLEAR` → `ELEVATED` → `JAMMED` → `DENIED`. Every
contribution is shown separately on the dashboard, because a verdict you cannot
audit is a verdict nobody should act on.

Full detail, including what this **cannot** do: [docs/DETECTION.md](docs/DETECTION.md)

## Hardware

| Part | Notes |
|---|---|
| Seeed XIAO ESP32-C5 | Any ESP32-C5 board |
| u-blox GNSS ×1–2 | **Must be u-blox** — M10/M9N ideal, M8N fine |
| USB power bank | Runs all day |

> **The one thing that will bite you:** cheap non-u-blox modules (ATGM336H,
> generic MTK) emit NMEA only and have **no jamming indicator**. Savethesat
> falls back to C/N0 and fix-loss on those, but you lose the two strongest
> signals. Buy u-blox.

[Full BOM and wiring →](docs/HARDWARE.md)

## Building from source

No external libraries — the NMEA and UBX parsing is in-tree, so there is
nothing to install beyond the ESP32 core.

```bash
arduino-cli core install esp32:esp32@3.3.0
arduino-cli compile --fqbn "esp32:esp32:esp32c5:CDCOnBoot=cdc" firmware/savethesat_c5
arduino-cli upload -p /dev/ttyACM0 --fqbn "esp32:esp32:esp32c5:CDCOnBoot=cdc" firmware/savethesat_c5
```

Pushing to `main` builds the firmware, merges the image and redeploys the
flasher automatically.

## Limits, stated plainly

- **One device detects; it does not locate.** Finding a jammer needs motion or
  several nodes. The ESP-NOW link is there to make the second possible.
- **Two receivers do not give a bearing.** Two antennas centimetres apart
  cannot resolve direction at L1. What the second one buys you is a reference
  channel and a cross-check.
- **No signature analysis.** Telling a chirp jammer from broadband noise needs
  real spectrum, which needs an SDR, which needs a host — a Raspberry Pi base
  station, one per *network*, not per sensor. See [docs/ROADMAP.md](docs/ROADMAP.md).
- **Spoofing is a different problem** and is not detected. A spoofer raises no
  noise floor; the receiver tracks a convincing lie.

## Rules of the road

Savethesat listens. It never transmits in a GNSS band, and that is a permanent
non-goal rather than an unimplemented feature. Detecting and mapping
interference is lawful nearly everywhere. Transmitting in these bands is not,
anywhere. If you find something, the next move belongs to people with legal
authority — report it.

## License

MIT. See [LICENSE](LICENSE).
