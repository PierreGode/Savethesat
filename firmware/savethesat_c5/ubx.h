/* Minimal UBX framing — no external libraries.
 *
 * We only ever POLL. Polling (a zero-length request for a message) is
 * supported across M8/M9/M10 and needs no configuration write, so the same
 * firmware works on a receiver whose config we have never touched and whose
 * settings survive a power cycle untouched.
 */
#pragma once
#include <Arduino.h>

#define UBX_CLASS_MON 0x0A
#define UBX_ID_MON_HW 0x09   /* M8: jamInd, agcCnt, noisePerMS            */
#define UBX_ID_MON_RF 0x38   /* M9/M10: the same, plus per-band jammingState */

#define UBX_MAX_PAYLOAD 320

/* Ask a receiver for one message. */
inline void ubxPoll(Stream &s, uint8_t cls, uint8_t id) {
  uint8_t f[8] = { 0xB5, 0x62, cls, id, 0x00, 0x00, 0x00, 0x00 };
  uint8_t a = 0, b = 0;
  for (int i = 2; i < 6; i++) { a += f[i]; b += a; }
  f[6] = a; f[7] = b;
  s.write(f, 8);
}

class UbxParser {
 public:
  typedef void (*Handler)(void *ctx, uint8_t cls, uint8_t id,
                          const uint8_t *payload, uint16_t len);

  void begin(Handler h, void *ctx) { h_ = h; ctx_ = ctx; state_ = 0; }

  void feed(uint8_t b) {
    switch (state_) {
      case 0: if (b == 0xB5) state_ = 1; break;
      case 1: state_ = (b == 0x62) ? 2 : 0; break;
      case 2: cls_ = b; ckA_ = b; ckB_ = b; state_ = 3; break;
      case 3: id_ = b; sum(b); state_ = 4; break;
      case 4: len_ = b; sum(b); state_ = 5; break;
      case 5:
        len_ |= (uint16_t)b << 8; sum(b); idx_ = 0;
        if (len_ > UBX_MAX_PAYLOAD) { state_ = 0; break; }
        state_ = len_ ? 6 : 7;
        break;
      case 6:
        buf_[idx_++] = b; sum(b);
        if (idx_ >= len_) state_ = 7;
        break;
      case 7: rxA_ = b; state_ = 8; break;
      case 8:
        if (rxA_ == ckA_ && b == ckB_ && h_) h_(ctx_, cls_, id_, buf_, len_);
        state_ = 0;
        break;
    }
  }

 private:
  void sum(uint8_t b) { ckA_ += b; ckB_ += ckA_; }

  Handler  h_   = nullptr;
  void    *ctx_ = nullptr;
  uint8_t  state_ = 0, cls_ = 0, id_ = 0, ckA_ = 0, ckB_ = 0, rxA_ = 0;
  uint16_t len_ = 0, idx_ = 0;
  uint8_t  buf_[UBX_MAX_PAYLOAD];
};
