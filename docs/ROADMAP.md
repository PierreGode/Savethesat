# Roadmap

## v1 — the standalone sensor (this release)

A self-contained ESP32-C5 that detects GNSS interference, shows its working on
a phone-friendly dashboard, and broadcasts its verdict to other nodes.

- [x] Two u-blox receivers, `UBX-MON-RF` / `UBX-MON-HW` polled at 1 Hz
- [x] Self-learned quiet baseline, frozen during events
- [x] Transparent five-signal score with per-contribution display
- [x] Access point with no uplink, so phones keep mobile data
- [x] 10-minute history chart and CSV export
- [x] ESP-NOW broadcast of verdict and position
- [x] Browser flasher on GitHub Pages

## v2 — locating, not just detecting

One sensor detects. Several sensors locate. This is the part that matters.

- [ ] **Aggregation.** Collect node reports into one map rather than a table.
- [ ] **Power-gradient mapping.** Log score against position while moving and
      render a heatmap. The pragmatic way to find a jammer: drive, watch the
      score climb, turn toward the rise.
- [ ] **Dead reckoning through denial.** The cruel irony of this problem is
      that when you are jammed you have lost your own position. Carry the last
      good fix forward so an event still lands somewhere on the map.
- [ ] **Persistent logging** to SD or flash, surviving power loss.

## v2 — the Raspberry Pi base station

One Pi for a whole network of sensors, never one per sensor.

- [ ] **RTL-SDR sweep of L1** (1570–1580 MHz) via `rtl_power`. This is what
      gives a *signature* — a chirp jammer looks nothing like broadband noise
      — and it keeps working when a receiver front end is saturated past the
      point of reporting anything useful.
- [ ] **Classification** from that signature.
- [ ] **Node aggregation** and a real map.

Note on hardware: an R820T tuner reaches 1575.42 MHz but sits at the very top
of its range, and needs an active GNSS antenna with an LNA and an external
bias-tee. Without that front end it only sees a jammer that is already close.

## Further out

- [ ] **TDOA multilateration** across nodes with disciplined clocks. This is
      the real answer to "where is it", and it is genuinely hard — the timing
      requirements are severe and the nodes lose their own time reference
      exactly when the jammer is present.
- [ ] **Spoofing detection.** A different problem from jamming, and currently
      out of scope. A spoofer raises no noise floor; the receiver tracks a
      convincing lie. Catching it needs independent time, position-jump
      detection, and C/N0 distributions that look *too* clean.

## Non-goals, permanently

- **Transmitting anything, ever, in any GNSS band.** Savethesat is receive
  only. This is not a limitation to be engineered around.
- **Jammer defeat or countermeasures.** Detecting, mapping and reporting is
  the project. What happens next belongs to people with legal authority.
