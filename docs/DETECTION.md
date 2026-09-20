# How Savethesat decides it is being jammed

A jammer will not identify itself, and "my GPS stopped working" is not
evidence — a parking garage does that too. What separates interference from
an obstructed sky is measurable, and a u-blox receiver reports all of it.

## The five signals

| Signal | Source | Why it matters | Weight |
|---|---|---|---|
| Jamming indicator | `UBX-MON-RF` / `UBX-MON-HW`, 0–255 | u-blox's own estimate of in-band interference. The most direct measurement available. | 35 |
| C/N0 collapse | NMEA `GSV`, mean of the strongest 8 satellites | Carrier-to-noise falls as the noise floor rises, even while the satellites remain in the almanac. | 25 |
| AGC level | `UBX-MON-RF`, 0–8191 | Automatic gain **falls** as input power rises. A sharp drop means something loud arrived. | 20 |
| Noise per ms | `UBX-MON-RF` | Broadband noise measured at the front end. | 10 |
| Fix lost, satellites visible | NMEA `GGA` + `GSV` | The classic signature. No sky means no satellites; jamming means satellites you can see but cannot use. | 10 |

The weighted sum is a 0–100 score:

| Score | Level |
|---|---|
| 0–24 | `CLEAR` |
| 25–49 | `ELEVATED` |
| 50–79 | `JAMMED` |
| 80–100 | `DENIED` |

A receiver reporting `UBX-MON-RF` also gives a `jammingState` field of its own.
When it says *critical*, that outranks our arithmetic and the level is forced
to `DENIED`. The receiver's own front end knows better than our weights do.

**The dashboard shows every contribution separately as its own bar.** This is
deliberate. A score that announces `JAMMED` without showing which signals drove
it is not something anyone should act on, and a field operator needs to be able
to disagree with the device.

## The baseline

Absolute numbers are meaningless — every antenna, every mounting position and
every site has its own quiet level. So the device learns one.

For the first 60 seconds after a receiver starts producing data, Savethesat
averages the jamming indicator, AGC, noise and C/N0, and calls that "quiet
here." Everything afterwards is measured as a *departure* from it.

The baseline then drifts by a very slow EWMA — but **only while the level is
`CLEAR`**. This matters more than it looks: a baseline that kept updating
during an event would quietly teach the device that the jammer is normal, and
a slowly ramping jammer would never be detected at all.

Press **Reset baseline** after moving the device, changing antennas, or any
time you know conditions are good and want it to relearn.

## Why two receivers

Two modules do **not** give you a bearing. Two antennas centimetres apart on
one board cannot resolve direction at L1, and anyone claiming otherwise is
selling something. What they actually buy you:

- **Cross-check.** One receiver denied while the other is fine means something
  local, not a constellation-wide event.
- **The reference channel.** Set `GPS_B_IS_REFERENCE` to `1` and deliberately
  attenuate or shield antenna B. A rise on A with B still calm is local
  interference arriving from a direction A can see and B cannot. This is the
  single best discriminator against the most common false positive, which is
  simple obstruction.
- **Vendor diversity**, against a firmware blind spot in one module.

Direction comes from *movement*: drive, watch the score climb, turn toward the
rise. Position comes from *several nodes* — which is what the ESP-NOW link
exists to enable.

## What this cannot do

- **It cannot classify the jammer.** Distinguishing a chirp jammer from a
  broadband noise source from a legitimate transmitter's spurious emission
  needs actual spectrum. See [ROADMAP.md](ROADMAP.md).
- **It cannot locate a jammer from one device.** One sensor detects. Locating
  requires either motion or multiple nodes.
- **It cannot detect spoofing.** A spoofer raises no noise floor and may not
  move the jamming indicator at all — the receiver is happily tracking a
  convincing lie. Detecting that needs cross-checks Savethesat does not yet do
  (time drift against an independent clock, position jumps, C/N0 that is *too*
  uniform).

## NMEA-only receivers (ATGM336H and friends)

Plenty of cheap modules — the ATGM336H / AT6558, most generic MTK parts —
speak NMEA and nothing else. No UBX means no jamming indicator, no AGC and no
noise figure: three of the five signals, and 65 of the 100 weight, simply are
not there.

Savethesat still works on these, from C/N0 collapse and fix loss. The score is
a weighted **mean over the signals a receiver can actually supply**, not a
fixed 100-point scale, so the missing weight is renormalised away rather than
making the upper levels unreachable. Without that, such a module would cap at
35 and could never report `JAMMED` no matter how completely it was denied.

What you give up is worth being clear about:

- **Later warning.** The jamming indicator and AGC move while the receiver is
  still tracking fine. C/N0 collapse means it is already losing the fight, so
  a NMEA-only device notices interference that is already serious.
- **More false positives.** C/N0 and fix loss are exactly the signals that a
  tunnel, a car roof or a bad antenna placement also move. With AGC gone, the
  cross-check that separates "something loud arrived" from "the sky went away"
  is gone with it.
- **No receiver second opinion.** `jammingState` from the receiver's own front
  end is not available to outrank a bad score.

The dashboard marks such a receiver plainly, greys out the three signals it
cannot supply, and says the verdict rests on C/N0 and fix loss alone.

**The reference channel matters much more here.** With no AGC to tell you
whether input power rose, a second antenna — deliberately attenuated — is the
best remaining discriminator between interference and obstruction. See the
section above.

If you have one NMEA-only module already soldered down, the cheapest useful
upgrade is not to replace it but to add a u-blox on the second port. You then
get the full five signals from one receiver and a cross-check from the other.

## Receiver requirements

The interference telemetry comes from `UBX-MON-RF` (M9, M10) or `UBX-MON-HW`
(M8). Savethesat polls both once per second and uses whichever answers, so no
receiver configuration is written and your module's settings are left alone.

**Non-u-blox modules lose the primary detection layer.** ATGM336H, generic MTK
and most cheap modules emit NMEA only and have no jamming indicator. Savethesat
degrades to C/N0 and fix loss on those, as described above — usable, later to
warn, and more prone to false positives.
