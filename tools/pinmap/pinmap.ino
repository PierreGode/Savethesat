/* Savethesat board bring-up tool.
 *
 * Temporary diagnostic firmware for working out what is actually wired to an
 * unfamiliar board — written because a GNSS module documented on GPIO 12/11
 * produced not one byte, and no amount of re-reading pin maps was going to
 * settle whether the fault was configuration, wiring or power.
 *
 * SAFETY — pins this never touches:
 *   GPIO 13, 14   USB D-/D+. Reconfiguring these drops the USB console
 *                 mid-run and the board then kills its own link on every
 *                 boot, needing a physical BOOT-button recovery.
 *   GPIO 15-22    SPI flash. Probing GPIO15 locked the CPU hard
 *                 (rst:0x1a CPU_LOCKUP, then "SPI flash busy" and an
 *                 unbootable image) — reconfiguring these cuts the chip off
 *                 from the flash it is executing from.
 *
 * Nothing here drives a pin as a push-pull output. The enable-pin hunt uses
 * the internal pull-up only: roughly 45k, which can satisfy a high-impedance
 * enable input but cannot source enough current to fight another driver or
 * damage anything on the other end.
 */
#include <Arduino.h>
#include <Wire.h>

/* Everything except USB (13,14) and the SPI flash region (15-22). */
static const int PINS[] = { 0,1,2,3,4,5,6,7,8,9,10,11,12,23,24,25,26,27,28 };
static const int NPINS = sizeof(PINS) / sizeof(PINS[0]);

static const uint32_t BAUDS[] = { 9600, 38400, 115200, 4800, 57600, 19200 };

static HardwareSerial Probe(1);

/* Documented GNSS pins for this board, the reference case. */
#define DOC_RX 12
#define DOC_TX 11

static void banner(const char *s) {
  Serial.printf("\n=== %s ===\n", s);
  Serial.flush();
}

/* ---------------------------------------------------------------- phase 1 */
/* Classic tri-state test: a pin that follows the internal pull in both
 * directions is floating; one that refuses to follow is being driven. */
static void census() {
  banner("PHASE 1  electrical census");
  Serial.println("pin  float pullup pulldn  edges  verdict");

  for (int i = 0; i < NPINS; i++) {
    int p = PINS[i];

    pinMode(p, INPUT);
    delay(15);
    uint32_t edges = 0;
    int last = digitalRead(p);
    uint32_t t0 = millis();
    while (millis() - t0 < 200) {
      int v = digitalRead(p);
      if (v != last) { edges++; last = v; }
    }
    int lf = digitalRead(p);

    pinMode(p, INPUT_PULLUP);   delay(15); int lu = digitalRead(p);
    pinMode(p, INPUT_PULLDOWN); delay(15); int ld = digitalRead(p);
    pinMode(p, INPUT);

    const char *verdict;
    if (edges > 50)        verdict = "TOGGLING  <<< signal";
    else if (lu == 0 && ld == 0) verdict = "DRIVEN LOW";
    else if (lu == 1 && ld == 1) verdict = "DRIVEN HIGH";
    else                   verdict = "floating (nothing attached)";

    Serial.printf("%-4d %-5d %-6d %-6d  %-6lu %s\n",
                  p, lf, lu, ld, (unsigned long)edges, verdict);
    Serial.flush();
  }
}

/* ---------------------------------------------------------------- phase 2 */
static uint16_t listenOn(int rx, uint32_t baud, uint32_t ms, bool show) {
  Probe.setRxBufferSize(1024);
  Probe.begin(baud, SERIAL_8N1, rx, -1);
  delay(60);
  while (Probe.available()) Probe.read();

  static uint8_t buf[512];
  uint16_t n = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < ms && n < sizeof buf)
    if (Probe.available()) buf[n++] = Probe.read();
  Probe.end();
  delay(10);

  if (n && show) {
    Serial.printf("  >>> %u bytes on GPIO%d @%lu : ", n, rx, (unsigned long)baud);
    for (uint16_t i = 0; i < n && i < 70; i++)
      Serial.write((buf[i] >= 32 && buf[i] < 127) ? buf[i] : '.');
    Serial.println();
    Serial.flush();
  }
  return n;
}

static void uartSweep() {
  banner("PHASE 2  UART sweep, every pin as receive");
  Serial.println("Full baud sweep on the documented pin, then 9600/38400 elsewhere.");

  Serial.printf("GPIO%d (documented GNSS RX):\n", DOC_RX);
  for (uint8_t b = 0; b < 6; b++) {
    uint16_t n = listenOn(DOC_RX, BAUDS[b], 1500, true);
    Serial.printf("  %lu baud -> %u bytes\n", (unsigned long)BAUDS[b], n);
    Serial.flush();
  }

  Serial.println("other pins:");
  for (int i = 0; i < NPINS; i++) {
    int p = PINS[i];
    if (p == DOC_RX) continue;
    uint16_t a = listenOn(p, 9600, 700, true);
    uint16_t c = listenOn(p, 38400, 700, true);
    if (a || c) Serial.printf("  GPIO%-2d -> 9600:%u  38400:%u\n", p, a, c);
  }
  Serial.println("  (pins not listed produced nothing)");
  Serial.flush();
}

/* ---------------------------------------------------------------- phase 3 */
/* If the module has an enable or standby input tied to a GPIO that nothing
 * currently drives, it will sit silent forever. Assert each candidate with
 * the internal pull-up in turn and watch the documented receive pin. */
static void enableHunt() {
  banner("PHASE 3  enable-pin hunt (pull-up only, no push-pull drive)");
  Serial.println("asserting each pin weakly, then listening on the GNSS pin");

  bool found = false;
  for (int i = 0; i < NPINS; i++) {
    int p = PINS[i];
    if (p == DOC_RX || p == DOC_TX) continue;

    pinMode(p, INPUT_PULLUP);
    delay(250);                       /* let a module boot and start talking */
    uint16_t n = listenOn(DOC_RX, 9600, 900, true);
    pinMode(p, INPUT);

    if (n) {
      Serial.printf("  *** GPIO%d LOOKS LIKE AN ENABLE: %u bytes appeared ***\n", p, n);
      found = true;
      Serial.flush();
    }
  }
  if (!found) Serial.println("  no pin unlocked the receiver");
  Serial.flush();
}

/* ---------------------------------------------------------------- phase 4 */
/* A module that is powered but mute may still answer a direct question. */
static void stimulus() {
  banner("PHASE 4  stimulus and response on the documented pair");

  for (uint8_t b = 0; b < 3; b++) {
    uint32_t baud = BAUDS[b];
    Probe.setRxBufferSize(1024);
    Probe.begin(baud, SERIAL_8N1, DOC_RX, DOC_TX);
    delay(80);
    while (Probe.available()) Probe.read();

    /* CASIC/PCAS product query — the ATGM336H family answers this. */
    Probe.print("$PCAS06,0*1B\r\n");
    /* u-blox UBX-MON-VER poll, in case it is not what the label says. */
    const uint8_t ver[8] = { 0xB5,0x62,0x0A,0x04,0x00,0x00,0x0E,0x34 };
    Probe.write(ver, 8);
    Probe.flush();

    uint8_t buf[256];
    uint16_t n = 0;
    uint32_t t0 = millis();
    while (millis() - t0 < 1200 && n < sizeof buf)
      if (Probe.available()) buf[n++] = Probe.read();
    Probe.end();
    delay(10);

    Serial.printf("  @%lu baud -> %u bytes", (unsigned long)baud, n);
    if (n) {
      Serial.print(" : ");
      for (uint16_t i = 0; i < n && i < 70; i++)
        Serial.write((buf[i] >= 32 && buf[i] < 127) ? buf[i] : '.');
    }
    Serial.println();
    Serial.flush();
  }
}

/* ---------------------------------------------------------------- phase 5 */
static void i2cScan(int sda, int scl) {
  Wire.end(); delay(20);
  if (!Wire.begin(sda, scl, 100000)) {
    Serial.printf("  SDA=%d SCL=%d : bus init failed\n", sda, scl);
    return;
  }
  delay(40);
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  SDA=%d SCL=%d : 0x%02X%s\n", sda, scl, a,
                    a == 0x42 ? "  <<< u-blox GNSS on I2C" :
                    (a == 0x3C || a == 0x3D ? "  (OLED)" : ""));
      found++;
    }
  }
  if (!found) Serial.printf("  SDA=%d SCL=%d : nothing\n", sda, scl);
  Wire.end();
  Serial.flush();
}

static void busScan() {
  banner("PHASE 5  I2C buses");
  i2cScan(23, 24);
  i2cScan(24, 23);
  i2cScan(0, 1);
  i2cScan(2, 3);
  i2cScan(4, 5);
  i2cScan(5, 4);
}

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("\n\n############ SAVETHESAT PIN MAPPER ############");
  Serial.printf("chip %s rev %d  flash %lu MB\n", ESP.getChipModel(),
                ESP.getChipRevision(),
                (unsigned long)(ESP.getFlashChipSize() / (1024 * 1024)));
  Serial.println("skipping GPIO 13,14 (USB) and 15-22 (SPI flash)");
  Serial.flush();

  census();
  uartSweep();
  enableHunt();
  stimulus();
  busScan();

  Serial.println("\n############ MAPPING COMPLETE ############");
  Serial.flush();
}

void loop() { delay(1000); }
