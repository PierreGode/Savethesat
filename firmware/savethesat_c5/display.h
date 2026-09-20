/* SSD1306 status screen.
 *
 * Probed at boot: a board with no panel fitted runs headless rather than
 * hanging, so one binary serves both.
 */
#pragma once
#include "config.h"

#if OLED_ENABLED
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "gnss.h"
#include "detect.h"

static Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);
static bool g_oledUp = false;

inline bool displayBegin() {
  Wire.begin(OLED_SDA, OLED_SCL, 400000);
  delay(50);
  Wire.beginTransmission(OLED_ADDR);
  if (Wire.endTransmission() != 0) return false;      /* nothing fitted */
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR, false, false)) return false;

  g_oledUp = true;
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 16);
  oled.println(F(" SAVETHESAT"));
  oled.setCursor(0, 32);
  oled.println(F(" starting..."));
  oled.display();
  return true;
}

inline void displayShow(const char *ssid, const char *ip, uint8_t level,
                        uint8_t score, uint16_t warmupLeft,
                        GnssRx &a, GnssRx &b, uint8_t peers) {
  if (!g_oledUp) return;
  oled.clearDisplay();

  /* header: name and uptime */
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print(F("SAVETHESAT"));
  uint32_t up = millis() / 1000;
  char t[12];
  snprintf(t, sizeof t, "%02lu:%02lu:%02lu",
           (unsigned long)(up / 3600), (unsigned long)((up / 60) % 60),
           (unsigned long)(up % 60));
  oled.setCursor(OLED_W - 6 * (int)strlen(t), 0);
  oled.print(t);
  oled.drawFastHLine(0, 9, OLED_W, SSD1306_WHITE);

  /* verdict, large */
  oled.setTextSize(2);
  oled.setCursor(0, 13);
  oled.print(levelName(level));

  /* score, or the warmup countdown */
  oled.setTextSize(1);
  oled.setCursor(0, 31);
  if (level == LVL_WARMUP) {
    if (warmupLeft) oled.printf("baseline in %us", warmupLeft);
    else            oled.print(F("waiting for GNSS"));
  } else {
    oled.printf("score %u/100", score);
  }

  /* one line per receiver */
  GnssRx *rx[2] = { &a, &b };
  for (int i = 0; i < 2; i++) {
    oled.setCursor(0, 41 + i * 9);
    if (!rx[i]->present) {
      oled.printf("%c --", rx[i]->label);
    } else {
      oled.printf("%c j%-3u c%-4.1f %u/%u%s", rx[i]->label, rx[i]->jamInd,
                  rx[i]->cn0Top, rx[i]->satsUsed, rx[i]->satsVisible,
                  rx[i]->fixValid ? "" : "!");
    }
  }

  /* footer: where to reach it */
  oled.drawFastHLine(0, 55, OLED_W, SSD1306_WHITE);
  oled.setCursor(0, 57);
  if (peers) oled.printf("%s +%u", ip, peers);
  else       oled.print(ip);
  (void)ssid;

  oled.display();
}
#else
inline bool displayBegin() { return false; }
inline void displayShow(const char *, const char *, uint8_t, uint8_t,
                        uint16_t, GnssRx &, GnssRx &, uint8_t) {}
#endif
