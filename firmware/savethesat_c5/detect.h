/* Turning receiver telemetry into a verdict.
 *
 * Nothing here is clever. It is deliberately a transparent weighted mean of
 * five independently meaningful signals, because a field operator has to be
 * able to look at the numbers and agree with the conclusion. An opaque score
 * that says JAMMED without showing why is worse than no score.
 *
 * Each signal is first reduced to how saturated it is, 0-100, independent of
 * its weight. The score is the weighted mean over the signals this particular
 * receiver can actually supply. That renormalisation matters: an NMEA-only
 * module offers no jamming indicator, no AGC and no noise figure, and a fixed
 * 100-point scale would cap such a device at 35 and make the upper levels
 * unreachable no matter how completely it was being denied.
 */
#pragma once
#include "config.h"
#include "gnss.h"

enum : uint8_t { LVL_WARMUP = 0, LVL_CLEAR, LVL_ELEVATED, LVL_JAMMED, LVL_DENIED };

/* Relative importance of each signal when it is available. */
#define W_JAM   35.0f
#define W_AGC   20.0f
#define W_NOISE 10.0f
#define W_CN0   25.0f
#define W_FIX   10.0f

struct Detection {
  uint8_t score = 0;
  uint8_t level = LVL_WARMUP;
  /* How saturated each signal is, 0-100, so the UI can show its working. */
  uint8_t pJam = 0, pAgc = 0, pNoise = 0, pCn0 = 0, pFix = 0;
  /* False when the receiver gives no UBX interference telemetry, so the
   * verdict rests on C/N0 and fix loss alone. */
  bool full = false;
};

inline const char *levelName(uint8_t l) {
  switch (l) {
    case LVL_CLEAR:    return "CLEAR";
    case LVL_ELEVATED: return "ELEVATED";
    case LVL_JAMMED:   return "JAMMED";
    case LVL_DENIED:   return "DENIED";
    default:           return "WARMUP";
  }
}

static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static inline uint8_t pct(float f) { return (uint8_t)(clamp01(f) * 100); }

inline Detection scoreRx(const GnssRx &rx) {
  Detection d;
  if (!rx.present || !rx.baseReady) return d;      /* stays WARMUP */

  d.full = rx.haveUbx;

  float sum = 0, total = 0;

  if (rx.haveUbx) {
    /* The jamming indicator is the single most direct measurement we get. */
    d.pJam = pct(((float)rx.jamInd - rx.baseJam) / 100.0f);
    /* AGC counts DOWN as the front end backs off from rising input power. */
    d.pAgc = pct((rx.baseAgc - (float)rx.agcCnt) / 2000.0f);
    d.pNoise = pct(((float)rx.noisePerMs - rx.baseNoise) / 200.0f);
    sum   += d.pJam * W_JAM + d.pAgc * W_AGC + d.pNoise * W_NOISE;
    total += W_JAM + W_AGC + W_NOISE;
  }

  /* Carrier-to-noise collapse across the satellites we can still see. */
  if (rx.baseCn0 > 0) {
    d.pCn0 = rx.cn0Top > 0 ? pct((rx.baseCn0 - rx.cn0Top) / 12.0f)
                           : 100;                  /* tracking nothing at all */
    sum   += d.pCn0 * W_CN0;
    total += W_CN0;
  }

  /* Losing the fix while satellites are still in view is the classic
   * signature — it separates jamming from simply having no sky. */
  d.pFix = (!rx.fixValid && rx.satsVisible >= 4) ? 100 : 0;
  sum   += d.pFix * W_FIX;
  total += W_FIX;

  float s = total > 0 ? sum / total : 0;

  /* A receiver that reports its own jamming state outranks our arithmetic. */
  if (rx.haveMonRf) {
    if (rx.jammingState == 2 && s < LEVEL_ELEVATED + 10) s = LEVEL_ELEVATED + 10;
    if (rx.jammingState == 3 && s < LEVEL_DENIED)        s = LEVEL_DENIED;
  }

  d.score = (uint8_t)(s > 100 ? 100 : s);
  if      (d.score >= LEVEL_DENIED)   d.level = LVL_DENIED;
  else if (d.score >= LEVEL_JAMMED)   d.level = LVL_JAMMED;
  else if (d.score >= LEVEL_ELEVATED) d.level = LVL_ELEVATED;
  else                                d.level = LVL_CLEAR;
  return d;
}
