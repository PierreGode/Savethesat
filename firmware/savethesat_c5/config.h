/* Savethesat — build-time configuration
 *
 * Every pin here goes through the GPIO matrix, so you can move any of them.
 * Defaults target a Seeed XIAO ESP32-C5 with two u-blox receivers.
 */
#pragma once

#define FW_VERSION "0.1.0"

/* ── GNSS receiver A — the open-sky antenna, primary detector ──────────── */
#define GPS_A_UART   1          /* HP UART1 */
#define GPS_A_RX     12         /* ESP RX  ← module TX */
#define GPS_A_TX     11         /* ESP TX  → module RX */
#define GPS_A_BAUD   9600

/* ── GNSS receiver B — second antenna, or the shielded reference channel ─ */
#define GPS_B_UART   0          /* HP UART0 — console is USB-Serial-JTAG, so it is free */
#define GPS_B_RX     4
#define GPS_B_TX     5
#define GPS_B_BAUD   9600

/* Treat B as a deliberately attenuated reference channel. When true, a rise
 * seen on A but NOT on B is reported as local interference rather than a
 * constellation-wide event. See docs/DETECTION.md. */
#define GPS_B_IS_REFERENCE 0

/* ── Access point ──────────────────────────────────────────────────────────
 * The AP intentionally provides no uplink and runs no captive-portal DNS.
 * That is what lets a phone keep using mobile data while viewing this page. */
#define AP_SSID_PREFIX "Savethesat"
#define AP_PASSWORD    "savethesat"   /* >= 8 chars, or "" for an open AP */
#define AP_CHANNEL     6              /* 2.4 GHz for phone compatibility */
#define MDNS_HOST      "savethesat"

/* ── Display ───────────────────────────────────────────────────────────────
 * SSD1306 OLED on the documented C5 I2C bus. Probed at boot; if it does not
 * answer, the firmware runs headless without complaint. */
#define OLED_ENABLED  1
#define OLED_SDA      23
#define OLED_SCL      24
#define OLED_ADDR     0x3C
#define OLED_W        128
#define OLED_H        64

/* ── Sampling and detection ───────────────────────────────────────────────── */
#define SAMPLE_INTERVAL_MS   1000
#define UBX_POLL_INTERVAL_MS 1000
#define HISTORY_LEN          600      /* 10 minutes at 1 Hz */
#define EVENT_LOG_LEN        40
#define RX_STALE_MS          5000     /* no data this long = receiver absent */
/* Serial log: one complete JSON object per line, this often. 0 disables. */
#define SERIAL_JSON_MS       1000

/* Baseline: what "quiet" looks like here. Learned once at startup, then
 * nudged by a slow EWMA only while the level is CLEAR. */
#define BASELINE_WARMUP_MS   60000
#define BASELINE_EWMA_ALPHA  0.002f

/* Level thresholds on the 0-100 score. */
#define LEVEL_ELEVATED  25
#define LEVEL_JAMMED    50
#define LEVEL_DENIED    80

/* ── ESP-NOW mesh ─────────────────────────────────────────────────────────── */
#define ESPNOW_ENABLED       1
#define ESPNOW_INTERVAL_MS   5000
#define ESPNOW_MAX_PEERS     16
#define ESPNOW_PEER_STALE_MS 60000
