/* One GNSS receiver: NMEA for position and per-satellite C/N0, UBX for the
 * interference telemetry that actually detects a jammer.
 *
 * No GPS library — the NMEA we need is three sentences, and pulling in a
 * parser would cost us more in CI pinning than it saves in code.
 */
#pragma once
#include <Arduino.h>
#include "config.h"
#include "ubx.h"

#define NMEA_MAX 100
#define SNR_WINDOW 48

class GnssRx {
 public:
  char     label = 'A';

  /* liveness */
  bool     present     = false;
  uint32_t lastDataMs  = 0;
  uint32_t firstDataMs = 0;

  /* position (NMEA) */
  bool     fixValid  = false;
  double   lat = 0, lon = 0;
  float    hdop = 99.0f;
  float    speedKn = 0;
  uint8_t  satsUsed = 0;
  uint8_t  fixQuality = 0;

  /* signal health (NMEA GSV) */
  uint8_t  satsVisible = 0;
  float    cn0Top = 0;        /* mean C/N0 of the strongest 8 satellites */
  uint8_t  cn0Tracked = 0;    /* satellites reporting a non-zero C/N0 */

  /* interference telemetry (UBX) */
  bool     haveUbx      = false;
  bool     haveMonRf    = false;   /* M9/M10 — gives jammingState too */
  uint8_t  jamInd       = 0;       /* 0-255, u-blox jamming indicator */
  uint8_t  jammingState = 0;       /* 0 unknown, 1 ok, 2 warning, 3 critical */
  uint16_t agcCnt       = 0;       /* 0-8191; FALLS as input power rises */
  uint16_t noisePerMs   = 0;
  uint8_t  antStatus    = 0;
  uint32_t lastUbxMs    = 0;

  /* learned quiet-time baseline */
  bool  baseReady = false;
  float baseJam = 0, baseAgc = 0, baseNoise = 0, baseCn0 = 0;

  void begin(char lbl, HardwareSerial *port, int uartNum, int rx, int tx, uint32_t baud) {
    label = lbl;
    port_ = port;
    port_->begin(baud, SERIAL_8N1, rx, tx);
    (void)uartNum;
    ubx_.begin(&GnssRx::onUbx, this);
  }

  /* Drain the UART. Call as often as you can — a GSV burst at 9600 baud
   * fills the 256-byte FIFO in well under a loop iteration's worth of slack. */
  void service() {
    while (port_ && port_->available()) {
      uint8_t b = (uint8_t)port_->read();
      ubx_.feed(b);
      feedNmea((char)b);
    }
    present = lastDataMs && (millis() - lastDataMs < RX_STALE_MS);
  }

  /* Ask for the interference telemetry. We poll both message types: a
   * receiver that lacks one simply NAKs it, which costs us nothing. */
  void pollUbx() {
    if (!port_) return;
    ubxPoll(*port_, UBX_CLASS_MON, UBX_ID_MON_RF);
    ubxPoll(*port_, UBX_CLASS_MON, UBX_ID_MON_HW);
  }

  /* Collapse the last second of GSV reports into one C/N0 figure. */
  void tick() {
    if (snrN_) {
      for (uint8_t i = 1; i < snrN_; i++) {      /* insertion sort, descending */
        float v = snr_[i]; int8_t j = i - 1;
        while (j >= 0 && snr_[j] < v) { snr_[j + 1] = snr_[j]; j--; }
        snr_[j + 1] = v;
      }
      uint8_t take = snrN_ < 8 ? snrN_ : 8;
      float sum = 0;
      for (uint8_t i = 0; i < take; i++) sum += snr_[i];
      cn0Top = sum / take;
      cn0Tracked = snrN_;
    } else {
      cn0Top = 0;
      cn0Tracked = 0;
    }
    snrN_ = 0;
    satsVisible = visAcc_;
    visAcc_ = 0;

    updateBaseline();
  }

  /* Forget what we learned and relearn from here. Use after moving the
   * device, changing antennas, or a known-good all-clear. */
  void resetBaseline() {
    baseReady = false;
    warmN_ = 0;
    warmJam_ = warmAgc_ = warmNoise_ = warmCn0_ = 0;
    firstDataMs = millis();
  }

  /* Let the scorer nudge the baseline only while things are calm. */
  void driftBaseline() {
    if (!baseReady) return;
    const float a = BASELINE_EWMA_ALPHA;
    baseJam   = (1 - a) * baseJam   + a * jamInd;
    baseAgc   = (1 - a) * baseAgc   + a * agcCnt;
    baseNoise = (1 - a) * baseNoise + a * noisePerMs;
    if (cn0Top > 0) baseCn0 = (1 - a) * baseCn0 + a * cn0Top;
  }

 private:
  HardwareSerial *port_ = nullptr;
  UbxParser ubx_;

  char     line_[NMEA_MAX];
  uint8_t  lineN_ = 0;
  float    snr_[SNR_WINDOW];
  uint8_t  snrN_ = 0;
  uint8_t  visAcc_ = 0;

  double   warmJam_ = 0, warmAgc_ = 0, warmNoise_ = 0, warmCn0_ = 0;
  uint32_t warmN_ = 0, warmCn0N_ = 0;

  void touch() {
    uint32_t now = millis();
    lastDataMs = now;
    if (!firstDataMs) firstDataMs = now;
  }

  void updateBaseline() {
    if (baseReady || !firstDataMs) return;
    if (haveUbx) {
      warmJam_ += jamInd; warmAgc_ += agcCnt; warmNoise_ += noisePerMs;
      warmN_++;
    }
    if (cn0Top > 0) { warmCn0_ += cn0Top; warmCn0N_++; }

    if (millis() - firstDataMs >= BASELINE_WARMUP_MS && (warmN_ || warmCn0N_)) {
      if (warmN_) {
        baseJam   = warmJam_   / warmN_;
        baseAgc   = warmAgc_   / warmN_;
        baseNoise = warmNoise_ / warmN_;
      }
      baseCn0 = warmCn0N_ ? (warmCn0_ / warmCn0N_) : 0;
      baseReady = true;
    }
  }

  /* ── UBX ──────────────────────────────────────────────────────────────── */
  static void onUbx(void *ctx, uint8_t cls, uint8_t id,
                    const uint8_t *p, uint16_t len) {
    GnssRx *self = (GnssRx *)ctx;
    self->touch();
    if (cls != UBX_CLASS_MON) return;

    if (id == UBX_ID_MON_RF && len >= 28) {
      /* header 4 bytes, then 24 bytes per RF block. Block 0 is L1. */
      const uint8_t *b = p + 4;
      self->jammingState = b[1] & 0x03;
      self->antStatus    = b[2];
      self->noisePerMs   = (uint16_t)b[12] | ((uint16_t)b[13] << 8);
      self->agcCnt       = (uint16_t)b[14] | ((uint16_t)b[15] << 8);
      self->jamInd       = b[16];
      self->haveUbx      = true;
      self->haveMonRf    = true;
      self->lastUbxMs    = millis();
    } else if (id == UBX_ID_MON_HW && len >= 60 && !self->haveMonRf) {
      /* M8 layout: noisePerMS@16, agcCnt@18, aStatus@20, jamInd@45 */
      self->noisePerMs = (uint16_t)p[16] | ((uint16_t)p[17] << 8);
      self->agcCnt     = (uint16_t)p[18] | ((uint16_t)p[19] << 8);
      self->antStatus  = p[20];
      self->jamInd     = p[45];
      self->haveUbx    = true;
      self->lastUbxMs  = millis();
    }
  }

  /* ── NMEA ─────────────────────────────────────────────────────────────── */
  void feedNmea(char c) {
    if (c == '\r') return;
    if (c == '\n') {
      if (lineN_) { line_[lineN_] = 0; parseNmea(); }
      lineN_ = 0;
      return;
    }
    if (c == '$') lineN_ = 0;
    if (lineN_ < NMEA_MAX - 1) line_[lineN_++] = c;
  }

  /* Returns field n of the current sentence into out, or "" if absent. */
  void field(uint8_t n, char *out, uint8_t outMax) {
    out[0] = 0;
    uint8_t f = 0, o = 0;
    for (uint8_t i = 0; i < lineN_; i++) {
      char c = line_[i];
      if (c == ',' || c == '*') {
        if (f == n) { out[o] = 0; return; }
        f++; o = 0;
        if (c == '*') return;
        continue;
      }
      if (f == n && o < outMax - 1) out[o++] = c;
    }
    if (f == n) out[o] = 0;
  }

  static double nmeaDeg(const char *v, const char *hemi) {
    if (!v[0]) return 0;
    double raw = atof(v);
    int deg = (int)(raw / 100);
    double d = deg + (raw - deg * 100) / 60.0;
    if (hemi[0] == 'S' || hemi[0] == 'W') d = -d;
    return d;
  }

  void parseNmea() {
    if (line_[0] != '$' || lineN_ < 7) return;
    touch();
    const char *t = line_ + 3;           /* skip "$xx" — any talker */
    char a[16], b[8];

    if (!strncmp(t, "GGA", 3)) {
      field(6, a, sizeof a); fixQuality = atoi(a);
      field(7, a, sizeof a); satsUsed = atoi(a);
      field(8, a, sizeof a); hdop = a[0] ? atof(a) : 99.0f;
      field(2, a, sizeof a); field(3, b, sizeof b);
      if (a[0]) lat = nmeaDeg(a, b);
      field(4, a, sizeof a); field(5, b, sizeof b);
      if (a[0]) lon = nmeaDeg(a, b);
      fixValid = fixQuality > 0;
    } else if (!strncmp(t, "RMC", 3)) {
      field(2, a, sizeof a);
      fixValid = (a[0] == 'A');
      field(7, a, sizeof a); if (a[0]) speedKn = atof(a);
    } else if (!strncmp(t, "GSV", 3)) {
      field(3, a, sizeof a);
      uint8_t vis = atoi(a);
      if (vis > visAcc_) visAcc_ = vis;
      /* up to four satellites per sentence: prn, elev, azim, cno */
      for (uint8_t s = 0; s < 4; s++) {
        field(7 + s * 4, a, sizeof a);
        if (!a[0]) continue;
        float v = atof(a);
        if (v > 0 && snrN_ < SNR_WINDOW) snr_[snrN_++] = v;
      }
    }
  }
};
