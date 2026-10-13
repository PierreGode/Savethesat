/* CSV logging to the SD card.
 *
 * The dashboard's live chart lives in RAM and dies with the power. Anything
 * worth going back to — a drive, an overnight watch, the hour either side of
 * an event — has to land on the card, in a format that opens in a spreadsheet
 * without ceremony.
 *
 * One file per session. The card is probed at boot and a board without one
 * runs unchanged, so the same binary serves both.
 */
#pragma once
#include "config.h"

#if SD_ENABLED
#include <SPI.h>
#include <SD.h>
#include "gnss.h"
#include "detect.h"

static bool     g_sdUp      = false;
static char     g_sdFile[40] = {0};
static uint32_t g_sdRows    = 0;
static uint32_t g_sdErrors  = 0;
static uint64_t g_sdSizeMB  = 0;
static File     g_sdHandle;
static uint16_t g_sdPending = 0;

static const char *SD_HEADER =
  "uptime_s,utc,level,score,lat,lon,fix,sats_used,sats_vis,"
  "cn0_a,jam_a,agc_a,noise_a,ant_a,"
  "cn0_b,jam_b,agc_b,tempC\n";

inline bool sdBegin() {
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, SPI, 20000000)) return false;
  if (SD.cardType() == CARD_NONE) return false;

  g_sdSizeMB = SD.cardSize() / (1024ULL * 1024ULL);
  if (!SD.exists(SD_DIR)) SD.mkdir(SD_DIR);

  /* One file per session, numbered. Without a fix there is no wall clock to
   * name it after, and a sequence number never collides. */
  for (uint16_t n = 1; n < 10000; n++) {
    char path[40];
    snprintf(path, sizeof path, SD_DIR "/log%04u.csv", n);
    if (SD.exists(path)) continue;
    g_sdHandle = SD.open(path, FILE_WRITE);
    if (!g_sdHandle) return false;
    g_sdHandle.print(SD_HEADER);
    g_sdHandle.flush();
    strncpy(g_sdFile, path, sizeof g_sdFile - 1);
    g_sdUp = true;
    return true;
  }
  return false;
}

inline void sdLogRow(uint8_t level, uint8_t score, GnssRx &a, GnssRx &b,
                     const char *levelStr, float tempC) {
  if (!g_sdUp) return;

  char row[220];
  int n = snprintf(row, sizeof row,
    "%lu,,%s,%u,%.6f,%.6f,%d,%u,%u,"
    "%.1f,%u,%u,%u,%s,"
    "%.1f,%u,%u,%.1f\n",
    (unsigned long)(millis() / 1000), levelStr, score,
    a.lat, a.lon, a.fixValid ? 1 : 0, a.satsUsed, a.satsVisible,
    a.cn0Top, a.jamInd, a.agcCnt, a.noisePerMs,
    a.antText[0] ? a.antText : "",
    b.cn0Top, b.jamInd, b.agcCnt, tempC);
  if (n <= 0) return;

  if (g_sdHandle.print(row) == 0) {
    g_sdErrors++;
    return;
  }
  g_sdRows++;
  if (++g_sdPending >= SD_FLUSH_ROWS) {
    g_sdHandle.flush();      /* a field unit loses power without warning */
    g_sdPending = 0;
  }
}

inline void sdJson(String &o) {
  o += "{\"present\":"; o += g_sdUp ? "true" : "false";
  o += ",\"file\":\"";  o += g_sdFile;
  o += "\",\"rows\":";  o += g_sdRows;
  o += ",\"errors\":";  o += g_sdErrors;
  o += ",\"sizeMB\":";  o += (uint32_t)g_sdSizeMB;
  o += "}";
}

/* Logs present on the card, newest last. */
inline void sdListJson(String &o) {
  o += "[";
  if (!g_sdUp) { o += "]"; return; }
  File dir = SD.open(SD_DIR);
  bool first = true;
  while (dir) {
    File f = dir.openNextFile();
    if (!f) break;
    if (!f.isDirectory()) {
      if (!first) o += ",";
      first = false;
      o += "{\"name\":\""; o += f.name();
      o += "\",\"bytes\":"; o += (uint32_t)f.size();
      o += "}";
    }
    f.close();
  }
  dir.close();
  o += "]";
}
#else
inline bool sdBegin() { return false; }
inline void sdLogRow(uint8_t, uint8_t, GnssRx &, GnssRx &, const char *, float) {}
inline void sdJson(String &o) { o += "{\"present\":false}"; }
inline void sdListJson(String &o) { o += "[]"; }
#endif
