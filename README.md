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

> **The one thing that will bite you:** cheap non-u-blox modules (ATGM336H /
> AT6558, generic MTK) emit NMEA only and have **no jamming indicator, no AGC
> and no noise figure** — three of the five signals. Savethesat runs on them
> from C/N0 collapse and fix loss, renormalising the score so the upper levels
> stay reachable, but it warns later and false-alarms more.
> [What you give up →](docs/DETECTION.md#nmea-only-receivers-atgm336h-and-friends)
>
> If one is already soldered down, adding a u-blox on the **second** port is a
> better move than replacing it: full signals from one, cross-check from both.

[Full BOM and wiring →](docs/HARDWARE.md)

## Building from source

No external libraries — the NMEA and UBX parsing is in-tree, so there is
nothing to install beyond the ESP32 core.

```bash
arduino-cli core install esp32:esp32@3.3.0
FQBN="esp32:esp32:esp32c5:CDCOnBoot=cdc,PartitionScheme=huge_app"
arduino-cli compile --fqbn "$FQBN" firmware/savethesat_c5
arduino-cli upload -p /dev/ttyACM0 --fqbn "$FQBN" firmware/savethesat_c5
```

Pushing to `main` builds the firmware, merges the image and redeploys the
flasher automatically.

## Measured board map

Taken from a real board with `tools/pinmap`, not from a datasheet. Every pin
read three ways — floating, pulled up, pulled down — so a pin that refuses to
follow the internal pull is being held by something external.

| GPIO | float | pull-up | pull-down | Reading | Documented as |
|---|---|---|---|---|---|
| 0–6 | 0 | 1 | 0 | floating | 0 = button |
| 7 | **1** | 1 | 0 | weak external pull-up | SD CS |
| 8, 9, 10 | 1 | 1 | **1** | **driven high** | SD SCK / MISO / MOSI |
| 11, 12 | 0 | 1 | 0 | **floating** | **GNSS TX / RX** |
| 23, 24 | 1 | 1 | **1** | **driven high** | I2C SDA / SCL |
| 25, 26 | 0 | 1 | 0 | floating | — |
| 27 | **1** | 1 | 0 | weak external pull-up | — |
| 28 | 1 | 1 | **1** | **driven high** | — |

- **GPIO 27 and 28 carry pulls that no published map explains.** 27 has a weak
  external pull-up, 28 is held high. Unidentified.

> **What this test cannot see.** A passive switch to ground reads as *floating*
> while it is open, because an open switch is an open circuit. So "floating" on
> GPIO 0 is perfectly consistent with a button being fitted there, and does not
> contradict the published map. An earlier revision of this table concluded the
> button had moved to GPIO 28 on that basis; that inference was wrong.

### Pins never to probe on the ESP32-C5

| GPIO | Why |
|---|---|
| 13, 14 | USB D−/D+. Reconfiguring them drops the USB console, and the board then kills its own link on every boot — recovery needs the BOOT button held during a physical replug. |
| 15–22 | SPI flash. Probing GPIO 15 locked the CPU (`rst:0x1a CPU_LOCKUP`), then `SPI flash busy` and an unbootable image. |

Both learned the hard way. `tools/pinmap` refuses to touch either range.

### A silent receiver: how this one was resolved

This board produced no GNSS data at all, and the cause turned out to be a
**badly seated antenna connector**. Worth recording, because the symptom is
deeply misleading: this module emits *nothing whatsoever* without a good
antenna connection — no NMEA, not even the empty "searching" sentences almost
every other receiver sends regardless. Its transmit pin stays idle, so the pin
reads floating and every test says "nothing attached".

Measurements taken while it was faulty, all of which pointed the wrong way:

- GPIO 11 and 12 read floating, following the internal pull in both directions
- No UART traffic at any of six baud rates, on any probeable pin
- Zero edges on all 19 probeable GPIOs
- No reply to a CASIC `$PCAS06` query or a u-blox `MON-VER` poll
- A second, unrelated firmware reported `chars=0` on the same pins

Every one of those was accurate, and the conclusion drawn from them — that the
module was not connected — was wrong. **A dead-silent GNSS module is an antenna
fault until proven otherwise**, and the solder joints are the last thing to
suspect, not the first.

### Check the antenna in software first

These modules say so themselves:

```
$GPTXT,01,01,01,ANTENNA OK*35
```

Savethesat parses that and shows it on the GNSS health card, in red when it is
anything but OK. One sentence would have settled the question immediately.

### A GSV gotcha worth knowing

NMEA 4.10 appends a **signal-id field** after the satellite blocks in a GSV
sentence. A receiver tracking nothing still sends:

```
$GPGSV,1,1,00,0*65
```

That trailing `0` sits exactly where the first satellite's PRN would be, so a
naive parser invents a phantom satellite every second. PRN 0 is never valid,
which is the cheapest reliable way to tell them apart.

## SD logging

With a card fitted, every sample is appended to `/savethesat/logNNNN.csv` —
one file per session, numbered, so a power cycle never overwrites the last run.

```
uptime_s,utc,level,score,lat,lon,fix,sats_used,sats_vis,
cn0_a,jam_a,agc_a,noise_a,ant_a,cn0_b,jam_b,agc_b,tempC
```

Plain CSV that opens in a spreadsheet without ceremony. Flushed every ten rows,
because a field unit loses power without warning and an unflushed buffer is a
lost event.

The **Storage** card on the dashboard shows the card size, the file being
written, the row count and any write errors, and lists every saved log with a
download link — so a session can be pulled off over Wi-Fi without unseating the
card.

A board with no card runs unchanged and says so. What you lose is permanence:
the trend chart is then RAM only and starts again from empty at every boot.

### Two timescales

| Chart | Source | Span | Resolution |
|---|---|---|---|
| Last 10 minutes | RAM | 10 min | 1 s |
| Trend | RAM | 6 hours | 30 s |
| CSV on the card | SD | the whole session | 1 s |

The 1 Hz buffer is for watching an event unfold. The trend is for noticing one
you were not watching. The card is for everything afterwards — the honest
record, at full resolution, long after both buffers have rolled over.

## Serial log

The USB console emits **one complete JSON object per line**, once a second —
the verdict, both receivers' detection state and link plumbing, per-constellation
counts, the full sky view, position, access point state, free heap and any mesh
peers. The same object is served at `/api/all`.

```bash
# watch it live
python3 -c "import serial;p=serial.Serial('/dev/ttyACM0',115200);[print(p.readline().decode().strip()) for _ in iter(int,1)]"

# or straight into jq
cat /dev/ttyACM0 | jq -c '{t,level,score,a:.a.link.bytes,b:.b.link.bytes}'

# log a session to file
cat /dev/ttyACM0 > session.ndjson
```

Each line also carries a `hw` block: chip model and revision, CPU clock, SDK,
MAC, flash and PSRAM size, **reset reason**, die temperature, heap free/min/
largest-block, the I2C bus contents found at boot, and the live logic level of
every GNSS UART pin.

> Read `rxLevel` carefully. The UART driver pulls its receive pin up, so `1`
> is the resting default and a disconnected pin reads exactly like a healthy
> idle one. Only `0` is informative — something is actively holding that line
> down. To tell "attached" from "not attached", use `bytes`.

Newline-delimited JSON, so it appends cleanly, greps usefully and replays into
anything. The interval is `SERIAL_JSON_MS` in `config.h`; set it to 0 to go
quiet.

One caveat: the ESP-ROM bootloader prints a few plain-text lines at power-on
before the firmware runs, and a panic would too. **Skip lines that do not
parse** rather than assuming every line is ours.

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
