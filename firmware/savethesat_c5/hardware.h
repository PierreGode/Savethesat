/* Hardware state, for the same log line as everything else.
 *
 * Static identity is read once at boot; the rest is sampled live.
 *
 * Read the pin levels carefully. The UART driver enables an internal pull-up
 * on its receive pin, so a 1 there is the resting default and says nothing
 * about whether a module is attached — a floating, disconnected pin reads 1
 * exactly like a healthy idle line does. Only a 0 carries information: with
 * the pull-up fighting it, something is actively holding that line down, which
 * means a short, a reversed connection, or a module clamping it through an
 * ESD diode while unpowered.
 *
 * To tell "attached and idle" from "not attached", use the byte counter: a
 * live transmitter produces bytes, a pull-up does not.
 */
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <esp_system.h>
#include <esp_chip_info.h>
#include <esp_mac.h>
#include "config.h"

#define HW_MAX_I2C 12

static char     g_hwChip[16]  = "?";
static char     g_hwMac[18]   = "?";
static uint8_t  g_hwRev       = 0;
static uint8_t  g_hwCores     = 0;
static uint32_t g_hwFlash     = 0;
static uint8_t  g_i2cAddr[HW_MAX_I2C];
static uint8_t  g_i2cN        = 0;

inline const char *resetReasonName() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "poweron";
    case ESP_RST_EXT:      return "external";
    case ESP_RST_SW:       return "software";
    case ESP_RST_PANIC:    return "panic";
    case ESP_RST_INT_WDT:  return "int_wdt";
    case ESP_RST_TASK_WDT: return "task_wdt";
    case ESP_RST_WDT:      return "wdt";
    case ESP_RST_DEEPSLEEP:return "deepsleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO:     return "sdio";
    case ESP_RST_USB:      return "usb";
    case ESP_RST_JTAG:     return "jtag";
    default:               return "unknown";
  }
}

/* Scan the I2C bus once. Called before the display is initialised so the two
 * cannot fight over the bus. */
inline void hwScanI2C() {
  Wire.begin(OLED_SDA, OLED_SCL, 100000);
  delay(30);
  g_i2cN = 0;
  for (uint8_t a = 1; a < 127 && g_i2cN < HW_MAX_I2C; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) g_i2cAddr[g_i2cN++] = a;
  }
}

inline void hwInit() {
  esp_chip_info_t ci;
  esp_chip_info(&ci);
  g_hwCores = ci.cores;
  g_hwRev   = ci.revision / 100;   /* IDF reports revision as major*100+minor */
  g_hwFlash = ESP.getFlashChipSize();
  snprintf(g_hwChip, sizeof g_hwChip, "%s", ESP.getChipModel());

  uint8_t m[6];
  esp_read_mac(m, ESP_MAC_WIFI_STA);
  snprintf(g_hwMac, sizeof g_hwMac, "%02X:%02X:%02X:%02X:%02X:%02X",
           m[0], m[1], m[2], m[3], m[4], m[5]);

  hwScanI2C();
}

inline const char *i2cName(uint8_t a) {
  switch (a) {
    case 0x3C: case 0x3D: return "OLED";
    case 0x42:            return "u-blox GNSS";
    case 0x68: case 0x69: return "IMU/RTC";
    case 0x76: case 0x77: return "pressure";
    default:              return "unknown";
  }
}

inline void hwJson(String &o) {
  o += "{\"chip\":\"";  o += g_hwChip;
  o += "\",\"rev\":";   o += g_hwRev;
  o += ",\"cores\":";   o += g_hwCores;
  o += ",\"cpuMHz\":";  o += getCpuFrequencyMhz();
  o += ",\"sdk\":\"";   o += ESP.getSdkVersion();
  o += "\",\"mac\":\""; o += g_hwMac;
  o += "\",\"flashMB\":"; o += g_hwFlash / (1024 * 1024);
  o += ",\"psramKB\":";   o += ESP.getPsramSize() / 1024;
  o += ",\"reset\":\"";   o += resetReasonName();
  o += "\",\"tempC\":";   o += String(temperatureRead(), 1);
  o += ",\"heap\":{\"free\":";  o += (uint32_t)ESP.getFreeHeap();
  o += ",\"min\":";             o += (uint32_t)ESP.getMinFreeHeap();
  o += ",\"maxBlock\":";        o += (uint32_t)ESP.getMaxAllocHeap();
  o += ",\"total\":";           o += (uint32_t)ESP.getHeapSize();
  o += "},\"i2c\":[";
  for (uint8_t i = 0; i < g_i2cN; i++) {
    if (i) o += ",";
    char b[8];
    snprintf(b, sizeof b, "0x%02X", g_i2cAddr[i]);
    o += "{\"addr\":\""; o += b;
    o += "\",\"is\":\"";  o += i2cName(g_i2cAddr[i]);
    o += "\"}";
  }
  /* Read-only: no pinMode, so the UART keeps the pin. See the header note —
   * 1 is the pulled-up default and is not evidence of anything; 0 is. */
  o += "],\"rxLevel\":{\"a\":"; o += digitalRead(GPS_A_RX);
  o += ",\"b\":";               o += digitalRead(GPS_B_RX);
  o += "},\"txLevel\":{\"a\":"; o += digitalRead(GPS_A_TX);
  o += ",\"b\":";               o += digitalRead(GPS_B_TX);
  o += "}}";
}
