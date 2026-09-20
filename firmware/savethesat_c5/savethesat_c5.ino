/* Savethesat — GPS jamming detector for the Seeed XIAO ESP32-C5
 *
 * Two u-blox receivers, an access point, a dashboard, and an ESP-NOW
 * broadcast so several of these can be worked as a network.
 *
 * RECEIVE ONLY. This firmware never transmits in a GNSS band.
 *
 * Licensed MIT. See LICENSE.
 */
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

#include "config.h"
#include "ubx.h"
#include "gnss.h"
#include "detect.h"
#include "espnow_link.h"
#include "display.h"
#include "webui.h"

static HardwareSerial SerialA(GPS_A_UART);
static HardwareSerial SerialB(GPS_B_UART);

static GnssRx gpsA, gpsB;
static Detection detA, detB;
static WebServer server(80);

/* Rolling history for the chart and the CSV export. */
static uint8_t  hJamA[HISTORY_LEN], hJamB[HISTORY_LEN], hScore[HISTORY_LEN], hLvl[HISTORY_LEN];
static uint16_t hHead = 0, hCount = 0;

static char g_ssid[33] = {0};
static char g_ip[17]   = {0};
static uint8_t g_level = LVL_WARMUP;
static uint8_t g_score = 0;
static bool    g_localOnly = false;

/* ── helpers ──────────────────────────────────────────────────────────────── */

static uint16_t warmupLeftS() {
  uint32_t now = millis();
  uint32_t worst = 0;
  GnssRx *rx[2] = { &gpsA, &gpsB };
  for (int i = 0; i < 2; i++) {
    if (!rx[i]->present || rx[i]->baseReady || !rx[i]->firstDataMs) continue;
    uint32_t done = now - rx[i]->firstDataMs;
    uint32_t left = done >= BASELINE_WARMUP_MS ? 0 : BASELINE_WARMUP_MS - done;
    if (left > worst) worst = left;
  }
  return worst / 1000;
}

static void pushHistory() {
  hJamA[hHead]  = gpsA.jamInd;
  hJamB[hHead]  = gpsB.jamInd;
  hScore[hHead] = g_score;
  hLvl[hHead]   = g_level;
  hHead = (hHead + 1) % HISTORY_LEN;
  if (hCount < HISTORY_LEN) hCount++;
}

static void rxJson(String &o, GnssRx &r, Detection &d) {
  o += "{\"present\":";  o += r.present ? "true" : "false";
  o += ",\"ubx\":";      o += r.haveUbx ? "true" : "false";
  o += ",\"monrf\":";    o += r.haveMonRf ? "true" : "false";
  o += ",\"level\":\"";  o += levelName(d.level);
  o += "\",\"score\":";  o += d.score;
  o += ",\"jam\":";      o += r.jamInd;
  o += ",\"agc\":";      o += r.agcCnt;
  o += ",\"noise\":";    o += r.noisePerMs;
  o += ",\"cn0\":";      o += String(r.cn0Top, 1);
  o += ",\"satsUsed\":"; o += r.satsUsed;
  o += ",\"satsVis\":";  o += r.satsVisible;
  o += ",\"fix\":";      o += r.fixValid ? "true" : "false";
  o += ",\"mod\":\"";    o += r.haveVer ? r.modName : "";
  o += "\",\"bytes\":";  o += r.rxBytes;
  o += ",\"baseJam\":";  o += String(r.baseJam, 1);
  o += ",\"baseAgc\":";  o += String(r.baseAgc, 0);
  o += ",\"baseCn0\":";  o += String(r.baseCn0, 1);
  o += ",\"pJam\":";     o += d.pJam;
  o += ",\"pAgc\":";     o += d.pAgc;
  o += ",\"pNoise\":";   o += d.pNoise;
  o += ",\"pCn0\":";     o += d.pCn0;
  o += ",\"pFix\":";     o += d.pFix;
  o += ",\"full\":";     o += d.full ? "true" : "false";
  o += "}";
}

static void handleStatus() {
  String o;
  o.reserve(1400);
  o  = "{\"fw\":\"" FW_VERSION "\",\"uptime\":";
  o += millis() / 1000;
  o += ",\"level\":\"";  o += levelName(g_level);
  o += "\",\"score\":";  o += g_score;
  o += ",\"warmup\":";   o += warmupLeftS();
  o += ",\"localOnly\":"; o += g_localOnly ? "true" : "false";
  o += ",\"a\":"; rxJson(o, gpsA, detA);
  o += ",\"b\":"; rxJson(o, gpsB, detB);
  o += ",\"peers\":[";
  bool first = true;
  for (int i = 0; i < ESPNOW_MAX_PEERS; i++) {
    if (!g_peers[i].used) continue;
    if (!first) o += ",";
    first = false;
    char mac[18];
    snprintf(mac, sizeof mac, "%02X:%02X:%02X:%02X:%02X:%02X",
             g_peers[i].mac[0], g_peers[i].mac[1], g_peers[i].mac[2],
             g_peers[i].mac[3], g_peers[i].mac[4], g_peers[i].mac[5]);
    o += "{\"mac\":\""; o += mac;
    o += "\",\"level\":\""; o += levelName(g_peers[i].pkt.level);
    o += "\",\"score\":"; o += g_peers[i].pkt.score;
    o += ",\"lat\":"; o += String(g_peers[i].pkt.lat1e7 / 1e7, 6);
    o += ",\"lon\":"; o += String(g_peers[i].pkt.lon1e7 / 1e7, 6);
    o += "}";
  }
  o += "]}";
  server.send(200, "application/json", o);
}

/* GNSS health: the link plumbing, per-constellation view and sky positions.
 * Kept off /api/status because the sky array is large and the dashboard polls
 * status every second. */
static void gnssJson(String &o, GnssRx &r, int uart, int rx, int tx, uint32_t baud) {
  uint32_t now = millis();
  o += "{\"present\":"; o += r.present ? "true" : "false";
  o += ",\"uart\":";    o += uart;
  o += ",\"rx\":";      o += rx;
  o += ",\"tx\":";      o += tx;
  o += ",\"baud\":";    o += baud;
  o += ",\"bytes\":";   o += r.rxBytes;
  /* Cast: the unsigned subtraction would turn the -1 sentinel into 4294967295. */
  o += ",\"nmeaAge\":"; o += r.lastNmeaMs ? (int32_t)((now - r.lastNmeaMs) / 1000) : -1;
  o += ",\"ubxAge\":";  o += r.lastUbxMs ? (int32_t)((now - r.lastUbxMs) / 1000) : -1;
  o += ",\"mod\":\"";  o += r.haveVer ? r.modName : "";
  o += "\",\"sw\":\"";  o += r.haveVer ? r.swVer : "";
  o += "\",\"hw\":\"";  o += r.haveVer ? r.hwVer : "";
  o += "\",\"telemetry\":\"";
  o += r.haveMonRf ? "MON-RF" : (r.haveUbx ? "MON-HW" : (r.present ? "none" : ""));
  o += "\",\"fix\":";  o += r.fixValid ? "true" : "false";
  o += ",\"ttff\":";    o += r.firstFixMs ? (int32_t)(r.firstFixMs / 1000) : -1;
  o += ",\"searching\":";
  o += r.fixValid ? 0 : (int32_t)((now - (r.noFixSince ? r.noFixSince : 0)) / 1000);
  o += ",\"antenna\":"; o += r.antStatus;
  o += ",\"cons\":[";
  bool first = true;
  for (uint8_t i = 0; i < NCONS; i++) {
    if (!r.consInView[i]) continue;
    if (!first) o += ",";
    first = false;
    o += "{\"name\":\""; o += CONS_NAMES[i];
    o += "\",\"inView\":"; o += r.consInView[i];
    o += ",\"tracked\":";  o += r.consTracked[i];
    o += ",\"snrMax\":";   o += r.consSnrMax[i];
    o += "}";
  }
  o += "],\"sky\":[";
  for (uint8_t i = 0; i < r.skyN; i++) {
    if (i) o += ",";
    o += "{\"c\":\""; o += CONS_NAMES[r.sky[i].cons];
    o += "\",\"prn\":"; o += r.sky[i].prn;
    o += ",\"el\":";     o += r.sky[i].elev;
    o += ",\"az\":";     o += r.sky[i].az;
    o += ",\"snr\":";    o += r.sky[i].snr;
    o += "}";
  }
  o += "]}";
}

static void handleGnss() {
  String o;
  o.reserve(4096);
  o = "{\"a\":";
  gnssJson(o, gpsA, GPS_A_UART, GPS_A_RX, GPS_A_TX, GPS_A_BAUD);
  o += ",\"b\":";
  gnssJson(o, gpsB, GPS_B_UART, GPS_B_RX, GPS_B_TX, GPS_B_BAUD);
  o += "}";
  server.send(200, "application/json", o);
}

static void handleHistory() {
  String o;
  o.reserve(HISTORY_LEN * 12 + 64);
  const char *names[3] = { "jamA", "jamB", "score" };
  uint8_t *srcs[3] = { hJamA, hJamB, hScore };
  o = "{";
  for (int s = 0; s < 3; s++) {
    if (s) o += ",";
    o += "\""; o += names[s]; o += "\":[";
    uint16_t start = (hHead + HISTORY_LEN - hCount) % HISTORY_LEN;
    for (uint16_t i = 0; i < hCount; i++) {
      if (i) o += ",";
      o += srcs[s][(start + i) % HISTORY_LEN];
    }
    o += "]";
  }
  o += "}";
  server.send(200, "application/json", o);
}

static void handleCsv() {
  String o;
  o.reserve(HISTORY_LEN * 24 + 64);
  o = "seconds_ago,jam_a,jam_b,score,level\n";
  uint16_t start = (hHead + HISTORY_LEN - hCount) % HISTORY_LEN;
  for (uint16_t i = 0; i < hCount; i++) {
    uint16_t k = (start + i) % HISTORY_LEN;
    o += (hCount - i) * (SAMPLE_INTERVAL_MS / 1000);
    o += ","; o += hJamA[k];
    o += ","; o += hJamB[k];
    o += ","; o += hScore[k];
    o += ","; o += levelName(hLvl[k]);
    o += "\n";
  }
  server.sendHeader("Content-Disposition", "attachment; filename=savethesat.csv");
  server.send(200, "text/csv", o);
}

static void handleRebase() {
  gpsA.resetBaseline();
  gpsB.resetBaseline();
  server.send(200, "application/json", "{\"ok\":true}");
}

/* ── setup ────────────────────────────────────────────────────────────────── */

void setup() {
  Serial.begin(115200);

  gpsA.begin('A', &SerialA, GPS_A_UART, GPS_A_RX, GPS_A_TX, GPS_A_BAUD);
  gpsB.begin('B', &SerialB, GPS_B_UART, GPS_B_RX, GPS_B_TX, GPS_B_BAUD);

  uint8_t mac[6];
  WiFi.mode(WIFI_AP);
  WiFi.macAddress(mac);
  char ssid[33];
  snprintf(ssid, sizeof ssid, "%s-%02X%02X", AP_SSID_PREFIX, mac[4], mac[5]);

  const char *pw = strlen(AP_PASSWORD) >= 8 ? AP_PASSWORD : nullptr;
  WiFi.softAP(ssid, pw, AP_CHANNEL);
  strncpy(g_ssid, ssid, sizeof g_ssid - 1);
  strncpy(g_ip, WiFi.softAPIP().toString().c_str(), sizeof g_ip - 1);

  /* No DNS server and no default route are offered on purpose — see the note
   * on the dashboard. A captive portal here would cost the operator their
   * mobile data connection. */
  if (MDNS.begin(MDNS_HOST)) MDNS.addService("http", "tcp", 80);

  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", INDEX_HTML);
  });
  server.on("/api/status",  HTTP_GET,  handleStatus);
  server.on("/api/history", HTTP_GET,  handleHistory);
  server.on("/api/gnss",    HTTP_GET,  handleGnss);
  server.on("/savethesat.csv", HTTP_GET, handleCsv);
  server.on("/api/reset-baseline", HTTP_POST, handleRebase);
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();

#if ESPNOW_ENABLED
  satnowBegin();
#endif

  Serial.printf("  OLED: %s\n", displayBegin() ? "found" : "not fitted");

  Serial.printf("\nSavethesat %s\n  AP  : %s\n  URL : http://%s/ or http://%s.local/\n",
                FW_VERSION, ssid, WiFi.softAPIP().toString().c_str(), MDNS_HOST);
  Serial.printf("  GNSS A: UART%d rx=%d tx=%d @%lu\n  GNSS B: UART%d rx=%d tx=%d @%lu\n",
                GPS_A_UART, GPS_A_RX, GPS_A_TX, (unsigned long)GPS_A_BAUD,
                GPS_B_UART, GPS_B_RX, GPS_B_TX, (unsigned long)GPS_B_BAUD);
}

/* ── loop ─────────────────────────────────────────────────────────────────── */

void loop() {
  gpsA.service();
  gpsB.service();
  server.handleClient();

  uint32_t now = millis();

  static uint32_t lastPoll = 0;
  if (now - lastPoll >= UBX_POLL_INTERVAL_MS) {
    lastPoll = now;
    gpsA.pollUbx();
    gpsB.pollUbx();
  }

  static uint32_t lastSample = 0;
  if (now - lastSample >= SAMPLE_INTERVAL_MS) {
    lastSample = now;
    gpsA.tick();
    gpsB.tick();

    detA = scoreRx(gpsA);
    detB = scoreRx(gpsB);

#if GPS_B_IS_REFERENCE
    /* B sits behind attenuation. A rising while B stays calm means the energy
     * is arriving locally rather than from a wide-area event. */
    g_score = detA.score;
    g_level = detA.level;
    g_localOnly = detA.level >= LVL_JAMMED && detB.level <= LVL_CLEAR;
#else
    g_score = detA.score > detB.score ? detA.score : detB.score;
    g_level = detA.level > detB.level ? detA.level : detB.level;
    g_localOnly = false;
#endif

    /* Only let the baseline drift while we believe things are quiet, or a
     * slowly ramping jammer would teach the device to ignore it. */
    if (g_level == LVL_CLEAR) { gpsA.driftBaseline(); gpsB.driftBaseline(); }

    pushHistory();

    displayShow(g_ssid, g_ip, g_level, g_score, warmupLeftS(),
                gpsA, gpsB, satnowPeerCount());
  }

#if DEBUG_INTERVAL_MS
  /* Heartbeat on the USB console, so a wired bench can see what the
   * receivers are doing without joining the access point. */
  static uint32_t lastDebug = 0;
  if (now - lastDebug >= DEBUG_INTERVAL_MS) {
    lastDebug = now;
    GnssRx *rx[2] = { &gpsA, &gpsB };
    Detection *dt[2] = { &detA, &detB };
    Serial.printf("[%6lus] %s score %u\n", now / 1000, levelName(g_level), g_score);
    for (int i = 0; i < 2; i++) {
      Serial.printf("   %c: %-7s bytes=%-8lu %s%s fix=%s sats=%u/%u cn0=%.1f jam=%u agc=%u\n",
        rx[i]->label,
        rx[i]->present ? "PRESENT" : "silent",
        (unsigned long)rx[i]->rxBytes,
        rx[i]->haveVer ? rx[i]->modName : (rx[i]->haveUbx ? "u-blox" : "no-UBX"),
        rx[i]->haveMonRf ? " [MON-RF]" : (rx[i]->haveUbx ? " [MON-HW]" : ""),
        rx[i]->fixValid ? "yes" : "no",
        rx[i]->satsUsed, rx[i]->satsVisible,
        rx[i]->cn0Top, rx[i]->jamInd, rx[i]->agcCnt);
      (void)dt[i];
    }
  }
#endif

#if ESPNOW_ENABLED
  static uint32_t lastBeacon = 0;
  if (now - lastBeacon >= ESPNOW_INTERVAL_MS) {
    lastBeacon = now;
    SatNowPacket p = {};
    p.magic   = SATNOW_MAGIC;
    p.version = SATNOW_VERSION;
    p.level   = g_level;
    p.score   = g_score;
    p.satsUsed = gpsA.satsUsed;
    p.jamA = gpsA.jamInd;  p.jamB = gpsB.jamInd;
    p.cn0A = (uint8_t)gpsA.cn0Top;  p.cn0B = (uint8_t)gpsB.cn0Top;
    p.fix  = gpsA.fixValid ? 1 : 0;
    p.lat1e7 = (int32_t)(gpsA.lat * 1e7);
    p.lon1e7 = (int32_t)(gpsA.lon * 1e7);
    p.uptimeS = now / 1000;
    satnowSend(p);
    satnowPeerCount();
  }
#endif
}
