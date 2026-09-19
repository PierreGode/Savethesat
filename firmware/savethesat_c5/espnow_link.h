/* ESP-NOW node-to-node link.
 *
 * One sensor detects. Several sensors locate — which is the whole point, so
 * the link ships in v1 even though the multilateration that will consume it
 * does not. Every node broadcasts its verdict and position; every node keeps
 * a table of what it heard. Connectionless, no pairing, no coordinator.
 */
#pragma once
#include <WiFi.h>
#include <esp_now.h>
#include "config.h"

#define SATNOW_MAGIC   0x314E5453UL   /* "STN1" */
#define SATNOW_VERSION 1

struct __attribute__((packed)) SatNowPacket {
  uint32_t magic;
  uint8_t  version;
  uint8_t  level;
  uint8_t  score;
  uint8_t  satsUsed;
  uint8_t  jamA, jamB;      /* jamming indicator, both receivers */
  uint8_t  cn0A, cn0B;      /* top-8 mean C/N0, rounded            */
  uint8_t  fix;
  int32_t  lat1e7, lon1e7;
  uint32_t uptimeS;
};

struct SatNowPeer {
  uint8_t      mac[6];
  SatNowPacket pkt;
  uint32_t     lastMs;
  bool         used;
};

static SatNowPeer g_peers[ESPNOW_MAX_PEERS];
static bool       g_espnowUp = false;

static void satnowRecv(const esp_now_recv_info_t *info,
                       const uint8_t *data, int len) {
  if (len != (int)sizeof(SatNowPacket)) return;
  SatNowPacket p;
  memcpy(&p, data, sizeof p);
  if (p.magic != SATNOW_MAGIC || p.version != SATNOW_VERSION) return;

  int free_ = -1;
  for (int i = 0; i < ESPNOW_MAX_PEERS; i++) {
    if (g_peers[i].used && !memcmp(g_peers[i].mac, info->src_addr, 6)) {
      g_peers[i].pkt = p; g_peers[i].lastMs = millis();
      return;
    }
    if (!g_peers[i].used && free_ < 0) free_ = i;
  }
  if (free_ >= 0) {
    memcpy(g_peers[free_].mac, info->src_addr, 6);
    g_peers[free_].pkt = p;
    g_peers[free_].lastMs = millis();
    g_peers[free_].used = true;
  }
}

inline bool satnowBegin() {
  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_recv_cb(satnowRecv);

  esp_now_peer_info_t bcast = {};
  memset(bcast.peer_addr, 0xFF, 6);
  bcast.channel = AP_CHANNEL;
  bcast.ifidx   = WIFI_IF_AP;
  bcast.encrypt = false;
  if (esp_now_add_peer(&bcast) != ESP_OK) return false;

  g_espnowUp = true;
  return true;
}

inline void satnowSend(const SatNowPacket &p) {
  if (!g_espnowUp) return;
  uint8_t bcast[6];
  memset(bcast, 0xFF, 6);
  esp_now_send(bcast, (const uint8_t *)&p, sizeof p);
}

inline uint8_t satnowPeerCount() {
  uint8_t n = 0;
  for (int i = 0; i < ESPNOW_MAX_PEERS; i++) {
    if (g_peers[i].used && millis() - g_peers[i].lastMs > ESPNOW_PEER_STALE_MS)
      g_peers[i].used = false;
    if (g_peers[i].used) n++;
  }
  return n;
}
