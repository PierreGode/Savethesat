/* Turning receiver telemetry into a verdict.
 *
 * Nothing here is clever. It is deliberately a transparent weighted sum of
 * five independently meaningful signals, because a field operator has to be
 * able to look at the numbers and agree with the conclusion. An opaque score
 * that says JAMMED without showing why is worse than no score.
 */
#pragma once
#include "config.h"
#include "gnss.h"

enum : uint8_t { LVL_WARMUP = 0, LVL_CLEAR, LVL_ELEVATED, LVL_JAMMED, LVL_DENIED };

struct Detection {
  uint8_t score = 0;
  uint8_t level = LVL_WARMUP;
  /* the contributions, exposed so the UI can show its working */
  uint8_t cJam = 0, cAgc = 0, cNoise = 0, cCn0 = 0, cFix = 0;
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

inline Detection scoreRx(const GnssRx &rx) {
  Detection d;
  if (!rx.present || !rx.baseReady) return d;   /* stays WARMUP */

  float s = 0;

  if (rx.haveUbx) {
    /* The jamming indicator is the single most direct measurement we get. */
    d.cJam = (uint8_t)(clamp01(((float)rx.jamInd - rx.baseJam) / 100.0f) * 35);
    /* AGC counts DOWN as the front end backs off from rising input power. */
    d.cAgc = (uint8_t)(clamp01((rx.baseAgc - (float)rx.agcCnt) / 2000.0f) * 20);
    d.cNoise = (uint8_t)(clamp01(((float)rx.noisePerMs - rx.baseNoise) / 200.0f) * 10);
  }

  /* Carrier-to-noise collapse across the satellites we can still see. */
  if (rx.baseCn0 > 0 && rx.cn0Top > 0)
    d.cCn0 = (uint8_t)(clamp01((rx.baseCn0 - rx.cn0Top) / 12.0f) * 25);
  else if (rx.baseCn0 > 0 && rx.cn0Top == 0)
    d.cCn0 = 25;    /* tracking nothing at all */

  /* Losing the fix while satellites are still in view is the classic
   * signature — it separates jamming from simply having no sky. */
  if (!rx.fixValid && rx.satsVisible >= 4) d.cFix = 10;

  s = d.cJam + d.cAgc + d.cNoise + d.cCn0 + d.cFix;

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
