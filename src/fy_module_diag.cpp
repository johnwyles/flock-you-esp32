// flock-you-esp32 — Optional-module diagnostics (GPS + CC1101) for M5Stack Basic
// See fy_module_diag.h for scope.
//
// Pin facts (M5Stack Basic M-Bus -> ESP32 GPIO), from the M5Stack docs for
// Module GPS v2.1 and Module CC1101:
//
//   GPS v2.1 (UART 115200 8N1)          | CC1101 (SPI)
//   GNSS_TX (-> ESP RX), DIP-selectable:| MOSI G23, MISO G19, SCK G18 (fixed)
//     MBus 2  = G35   MBus 13 = G3 (!) | CSn, DIP-selectable:
//     MBus 15 = G16   MBus 22 = G13    |   MBus 8 = G25 (speaker DAC!)
//     MBus 26 = G34                    |   MBus 21 = G12 (strapping)
//   GNSS_RX (<- ESP TX): G1(!) G17     |   MBus 23 = G15
//     G12(!) G15 G0                    |   MBus 24 = G0
//   PPS: G35 G36 G25                   | GDO0/GDO2: MBus 2 = G35,
//                                      |   MBus 20 = G5, MBus 22 = G13
//
// G3/G1 are the USB serial console, so a GPS whose DIP puts GNSS_TX on G3
// shows up as NMEA text arriving on the console (fy_serial.cpp flags that).

#include "fy_module_diag.h"
#include "fy_gps.h"
#include "fy_cc1101.h"
#include <HardwareSerial.h>
#include "driver/gpio.h"

FyGpsDiag    gGpsDiag;
FyCc1101Diag gCc1101Diag;

// ── helpers ──────────────────────────────────────────────────────────────────

static void appendf(char *buf, size_t len, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void appendf(char *buf, size_t len, const char *fmt, ...) {
  size_t used = strnlen(buf, len);
  if (used >= len - 1) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf + used, len - used, fmt, ap);
  va_end(ap);
}

const char *fyDiagPinLabel(int8_t gpio) {
  switch (gpio) {
    case 0:  return "G0/MBus24";
    case 3:  return "G3/MBus13";
    case 4:  return "G4(SD CS)";
    case 5:  return "G5/MBus20";
    case 12: return "G12/MBus21";
    case 13: return "G13/MBus22";
    case 15: return "G15/MBus23";
    case 16: return "G16/MBus15";
    case 25: return "G25/MBus8";
    case 34: return "G34/MBus26";
    case 35: return "G35/MBus2";
    case -1: return "none";
    default: return "?";
  }
}

static bool nmeaChecksumOk(const char *s) {
  if (s[0] != '$') return false;
  const char *star = strchr(s, '*');
  if (!star || strlen(star) < 3) return false;
  uint8_t sum = 0;
  for (const char *p = s + 1; p < star; p++) sum ^= (uint8_t)*p;
  return sum == (uint8_t)strtoul(star + 1, nullptr, 16);
}

// Listen on one source for up to windowMs; count bytes + checksum-valid NMEA.
struct NmeaListen {
  uint32_t bytes = 0;
  uint32_t dollars = 0;
  uint32_t valid = 0;
  char line[100] = {0};
  uint8_t idx = 0;
  char sample[84] = {0};

  void feed(char c) {
    bytes++;
    if (c == '$') { dollars++; idx = 0; }
    if (c == '\r' || c == '\n') {
      if (idx) {
        line[idx] = '\0';
        if (nmeaChecksumOk(line)) {
          valid++;
          if (!sample[0]) { strncpy(sample, line, sizeof(sample) - 1); }
        }
      }
      idx = 0;
      return;
    }
    if (idx < sizeof(line) - 1) line[idx++] = c;
  }
};

// ── GPS ──────────────────────────────────────────────────────────────────────

static void gpsI2cScan() {
  gGpsDiag.i2cDevices[0] = '\0';
  Wire.begin(21, 22);
  for (uint8_t a = 0x08; a <= 0x77; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      const char *name = "";
      switch (a) {
        case 0x10: name = "(GNSS?)"; break;
        case 0x42: name = "(u-blox?)"; break;
        case 0x68: name = "(MPU6886 IMU)"; break;
        case 0x6C: name = "(SH200Q IMU)"; break;
        case 0x75: name = "(IP5306 PMIC)"; break;
        default: break;
      }
      appendf(gGpsDiag.i2cDevices, sizeof(gGpsDiag.i2cDevices), "%s0x%02X%s",
              gGpsDiag.i2cDevices[0] ? " " : "", a, name);
    }
  }
  if (!gGpsDiag.i2cDevices[0]) strcpy(gGpsDiag.i2cDevices, "(none)");
}

// Try an I2C GNSS at addr: it must stream NMEA, not just ACK.
static bool gpsProbeI2c(uint8_t addr) {
  Wire.beginTransmission(addr);
  if (Wire.endTransmission() != 0) return false;
  NmeaListen l;
  uint32_t start = millis();
  while (millis() - start < 1500 && !l.valid) {
    int n = Wire.requestFrom((int)addr, 64);
    for (int i = 0; i < n && Wire.available(); i++) {
      char c = Wire.read();
      if (c != (char)0xFF) l.feed(c);
    }
    delay(20);
  }
  appendf(gGpsDiag.tried, sizeof(gGpsDiag.tried), "i2c 0x%02X: %lu B, %lu NMEA; ",
          addr, (unsigned long)l.bytes, (unsigned long)l.valid);
  if (l.valid) {
    strncpy(gGpsDiag.sample, l.sample, sizeof(gGpsDiag.sample) - 1);
    return true;
  }
  return false;
}

// Listen on Serial2 RX=pin at baud for windowMs.
static NmeaListen gpsListenUart(int8_t pin, uint32_t baud, uint32_t windowMs) {
  NmeaListen l;
  Serial2.end();
  Serial2.setRxBufferSize(1024);
  Serial2.begin(baud, SERIAL_8N1, pin, -1);
  delay(5);
  while (Serial2.available()) Serial2.read();  // drop junk from pin switch
  uint32_t start = millis();
  while (millis() - start < windowMs) {
    while (Serial2.available()) l.feed((char)Serial2.read());
    if (l.valid >= 2) break;  // two checksum-valid sentences = definitely a GNSS
    delay(2);
  }
  return l;
}

bool fyDiagProbeGps() {
  memset(&gGpsDiag, 0, sizeof(gGpsDiag));
  gGpsDiag.probed = true;
  gGpsDiag.rxPin = -1;
  uint32_t t0 = millis();

  Serial.println("[diag] ── GPS probe ─────────────────────────────────────");
  gpsI2cScan();
  Serial.printf("[diag] I2C devices on G21/G22: %s\n", gGpsDiag.i2cDevices);

  // UART candidates the GPS v2.1 DIP switch can route GNSS_TX to (G3 is the
  // USB console, so it cannot be probed here — see fy_serial.cpp).
  static const int8_t kRxPins[] = {16, 13, 35, 34};
  static const uint32_t kBauds[] = {115200, 9600, 38400, 57600};
  bool noisePin = false;
  const uint32_t kUartBudgetMs = 9000;  // cap boot delay if pins are noisy

  for (int8_t pin : kRxPins) {
    for (uint8_t b = 0; b < sizeof(kBauds) / sizeof(kBauds[0]); b++) {
      if (millis() - t0 > kUartBudgetMs) {
        appendf(gGpsDiag.tried, sizeof(gGpsDiag.tried), "(time budget hit) ");
        break;
      }
      NmeaListen l = gpsListenUart(pin, kBauds[b], 1300);
      Serial.printf("[diag]   UART RX=%-10s @%6lu: %4lu bytes, %3lu '$', %3lu valid NMEA\n",
                    fyDiagPinLabel(pin), (unsigned long)kBauds[b], (unsigned long)l.bytes,
                    (unsigned long)l.dollars, (unsigned long)l.valid);
      appendf(gGpsDiag.tried, sizeof(gGpsDiag.tried), "%s@%lu:%luB/%lu; ", fyDiagPinLabel(pin),
              (unsigned long)kBauds[b], (unsigned long)l.bytes, (unsigned long)l.valid);
      if (l.valid) {
        gGpsDiag.found = true;
        gGpsDiag.transport = GPS_TRANSPORT_UART;
        gGpsDiag.rxPin = pin;
        gGpsDiag.baud = kBauds[b];
        strncpy(gGpsDiag.sample, l.sample, sizeof(gGpsDiag.sample) - 1);
        break;
      }
      // Silent line at 115200 -> nothing is driving this pin; skip other bauds.
      if (l.bytes == 0) break;
      noisePin = true;  // bytes but no NMEA: wrong baud or floating input
    }
    if (gGpsDiag.found) break;
  }
  Serial2.end();

  if (!gGpsDiag.found) {
    const uint8_t addrs[] = {0x10, 0x42};
    for (uint8_t a : addrs) {
      if (gpsProbeI2c(a)) {
        gGpsDiag.found = true;
        gGpsDiag.transport = GPS_TRANSPORT_I2C;
        gGpsDiag.i2cAddr = a;
        break;
      }
    }
  }

  gGpsDiag.probeMs = millis() - t0;
  if (gGpsDiag.found) {
    if (gGpsDiag.transport == GPS_TRANSPORT_UART) {
      snprintf(gGpsDiag.hint, sizeof(gGpsDiag.hint),
               "NMEA stream found on %s @ %lu baud.",
               fyDiagPinLabel(gGpsDiag.rxPin), (unsigned long)gGpsDiag.baud);
    } else {
      snprintf(gGpsDiag.hint, sizeof(gGpsDiag.hint), "NMEA stream found on I2C 0x%02X.",
               gGpsDiag.i2cAddr);
    }
  } else if (noisePin) {
    snprintf(gGpsDiag.hint, sizeof(gGpsDiag.hint),
             "Bytes seen but no valid NMEA: check the GPS DIP switch (GNSS_TX -> G16) and "
             "that only ONE TX switch is ON. G34/G35 have no pull-up and can pick up noise.");
  } else {
    snprintf(gGpsDiag.hint, sizeof(gGpsDiag.hint),
             "No NMEA on G16/G13/G35/G34 or I2C. Check the module is seated on the M-Bus, "
             "a GNSS_TX DIP switch is ON (G16 recommended), and the antenna is attached. "
             "If NMEA text appears on this console, GNSS_TX is on G3 (USB RX).");
  }
  Serial.printf("[diag] GPS: %s  (%lu ms)\n", gGpsDiag.found ? "FOUND" : "NOT FOUND",
                (unsigned long)gGpsDiag.probeMs);
  if (gGpsDiag.sample[0]) Serial.printf("[diag] GPS sample: %s\n", gGpsDiag.sample);
  Serial.printf("[diag] GPS hint: %s\n", gGpsDiag.hint);
  return gGpsDiag.found;
}

// ── CC1101 ───────────────────────────────────────────────────────────────────

static const SPISettings kDiagSpi(1000000, MSBFIRST, SPI_MODE0);

static bool misoLowWithin(uint32_t us) {
  uint32_t s = micros();
  while (gpio_get_level(GPIO_NUM_19)) {
    if (micros() - s > us) return false;
  }
  return true;
}

// One raw transaction: header + optional data byte; returns last byte read.
static uint8_t ccXfer(uint8_t cs, uint8_t hdr, uint8_t data, bool twoBytes, bool *ready) {
  SPI.beginTransaction(kDiagSpi);
  digitalWrite(cs, LOW);
  bool r = misoLowWithin(3000);
  if (ready) *ready = r;
  uint8_t v = SPI.transfer(hdr);
  if (twoBytes) v = SPI.transfer(data);
  digitalWrite(cs, HIGH);
  SPI.endTransaction();
  return v;
}
static uint8_t ccRead(uint8_t cs, uint8_t reg) {
  uint8_t hdr = (reg >= 0x30 && reg <= 0x3D) ? (reg | 0xC0) : (reg | 0x80);
  return ccXfer(cs, hdr, 0x00, true, nullptr);
}
static void ccWrite(uint8_t cs, uint8_t reg, uint8_t v) { ccXfer(cs, reg, v, true, nullptr); }
static void ccStrobe(uint8_t cs, uint8_t cmd) { ccXfer(cs, cmd, 0, false, nullptr); }

// Identify which candidate GPIO follows GDOx when we force it low/high.
static int8_t ccFindGdo(uint8_t cs, uint8_t iocfgReg, const int8_t *pins, uint8_t nPins) {
  for (uint8_t i = 0; i < nPins; i++) pinMode(pins[i], INPUT);
  ccWrite(cs, iocfgReg, 0x2F);  // "HW to 0"
  delayMicroseconds(200);
  int lo[4];
  for (uint8_t i = 0; i < nPins; i++) lo[i] = digitalRead(pins[i]);
  ccWrite(cs, iocfgReg, 0x6F);  // "HW to 0" inverted -> 1
  delayMicroseconds(200);
  int8_t found = -1;
  for (uint8_t i = 0; i < nPins; i++) {
    if (lo[i] == LOW && digitalRead(pins[i]) == HIGH) { found = pins[i]; break; }
  }
  return found;
}

bool fyDiagProbeCc1101(bool sdInUse, bool loraPresent) {
  memset(&gCc1101Diag, 0, sizeof(gCc1101Diag));
  gCc1101Diag.probed = true;
  gCc1101Diag.csPin = gCc1101Diag.gdo0Pin = gCc1101Diag.gdo2Pin = -1;

  Serial.println("[diag] ── CC1101 probe ──────────────────────────────────");
  SPI.begin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, -1);
  // Pull-up on MISO so "nothing answering" reads as 0xFF / MISO-high instead
  // of floating. Uses the pad pull-up only; does not detach MISO from SPI.
  gpio_pullup_en(GPIO_NUM_19);

  // G25 is the speaker DAC, so it is tried last (only reached if nothing
  // else answered); main.cpp re-inits the speaker afterwards.
  int8_t csPins[5] = {15, 0, 12, 25, -1};
  uint8_t nCs = 4;
  if (!sdInUse) csPins[nCs++] = 4;  // legacy firmware default (SD CS on Basic)

  // Deselect every candidate first so only one chip can talk at a time.
  for (uint8_t i = 0; i < nCs; i++) { digitalWrite(csPins[i], HIGH); pinMode(csPins[i], OUTPUT); }
  delay(2);

  for (uint8_t i = 0; i < nCs; i++) {
    uint8_t cs = (uint8_t)csPins[i];
    bool ready = false;
    ccXfer(cs, CC1101_SNOP, 0, false, &ready);  // CSn low -> CHIP_RDYn on MISO
    if (!ready) {
      Serial.printf("[diag]   CS=%-10s: MISO stayed HIGH (no chip ready)\n", fyDiagPinLabel(cs));
      appendf(gCc1101Diag.tried, sizeof(gCc1101Diag.tried), "%s:no-ready; ", fyDiagPinLabel(cs));
      continue;
    }
    ccStrobe(cs, CC1101_SRES);
    delay(2);
    uint8_t part = ccRead(cs, CC1101_PARTNUM);
    uint8_t ver = ccRead(cs, CC1101_VERSION);
    uint8_t sync1Default = ccRead(cs, CC1101_SYNC1);  // reset value 0xD3
    ccWrite(cs, CC1101_SYNC1, 0xA5);
    uint8_t rb1 = ccRead(cs, CC1101_SYNC1);
    ccWrite(cs, CC1101_SYNC1, 0x5A);
    uint8_t rb2 = ccRead(cs, CC1101_SYNC1);
    ccWrite(cs, CC1101_SYNC1, 0xD3);
    bool ok = part == 0x00 && rb1 == 0xA5 && rb2 == 0x5A && ver != 0x00 && ver != 0xFF;
    Serial.printf("[diag]   CS=%-10s: ready, PARTNUM=0x%02X VERSION=0x%02X SYNC1=0x%02X "
                  "rw=0x%02X/0x%02X -> %s\n", fyDiagPinLabel(cs), part, ver, sync1Default,
                  rb1, rb2, ok ? "CC1101 PRESENT" : "no match");
    appendf(gCc1101Diag.tried, sizeof(gCc1101Diag.tried), "%s:part=%02X ver=%02X rw=%s; ",
            fyDiagPinLabel(cs), part, ver, (rb1 == 0xA5 && rb2 == 0x5A) ? "ok" : "fail");
    if (ok) {
      gCc1101Diag.found = true;
      gCc1101Diag.csPin = cs;
      gCc1101Diag.partnum = part;
      gCc1101Diag.version = ver;
      break;
    }
  }

  if (gCc1101Diag.found) {
    uint8_t cs = (uint8_t)gCc1101Diag.csPin;
    int8_t gdoPins[3];
    uint8_t n = 0;
    gdoPins[n++] = 35;
    if (!loraPresent) gdoPins[n++] = 5;
    gdoPins[n++] = 13;
    gCc1101Diag.gdo0Pin = ccFindGdo(cs, CC1101_IOCFG0, gdoPins, n);
    gCc1101Diag.gdo2Pin = ccFindGdo(cs, CC1101_IOCFG2, gdoPins, n);
    ccWrite(cs, CC1101_IOCFG0, 0x3F);  // reset defaults
    ccWrite(cs, CC1101_IOCFG2, 0x29);
    gCc1101Diag.marcstate = ccRead(cs, CC1101_MARCSTATE) & 0x1F;
    Serial.printf("[diag]   GDO0=%s GDO2=%s MARCSTATE=0x%02X\n",
                  fyDiagPinLabel(gCc1101Diag.gdo0Pin), fyDiagPinLabel(gCc1101Diag.gdo2Pin),
                  gCc1101Diag.marcstate);
  }

  // Release candidates we are not using so they don't fight other modules.
  for (uint8_t i = 0; i < nCs; i++) {
    if (csPins[i] != gCc1101Diag.csPin) pinMode(csPins[i], INPUT);
  }

  if (gCc1101Diag.found) {
    snprintf(gCc1101Diag.hint, sizeof(gCc1101Diag.hint), "CC1101 answering on CSn=%s%s",
             fyDiagPinLabel(gCc1101Diag.csPin),
             gCc1101Diag.csPin == 25 ? " (G25 is the speaker DAC; avoid beeps or move CSn to G15)"
             : gCc1101Diag.csPin == 12 ? " (G12 is a boot strapping pin; G15 is safer)" : ".");
  } else {
    snprintf(gCc1101Diag.hint, sizeof(gCc1101Diag.hint),
             "No CC1101 on CSn G15/G25/G0/G12. Check the module is seated on the M-Bus and "
             "exactly ONE CSn DIP switch is ON (G15 recommended on Basic).");
  }
  Serial.printf("[diag] CC1101: %s\n", gCc1101Diag.found ? "FOUND" : "NOT FOUND");
  Serial.printf("[diag] CC1101 hint: %s\n", gCc1101Diag.hint);
  return gCc1101Diag.found;
}

// ── Reporting ────────────────────────────────────────────────────────────────

void fyDiagPrint(Print &out) {
  out.println("[diag] ===== Module diagnostics =====");
  out.printf("[diag] GPS    : %s", gGpsDiag.probed ? (gGpsDiag.found ? "FOUND" : "NOT FOUND") : "not probed");
  if (gGpsDiag.found && gGpsDiag.transport == GPS_TRANSPORT_UART)
    out.printf(" on UART RX=%s @ %lu", fyDiagPinLabel(gGpsDiag.rxPin), (unsigned long)gGpsDiag.baud);
  if (gGpsDiag.found && gGpsDiag.transport == GPS_TRANSPORT_I2C)
    out.printf(" on I2C 0x%02X", gGpsDiag.i2cAddr);
  out.println();
  out.printf("[diag]   I2C bus : %s\n", gGpsDiag.i2cDevices);
  out.printf("[diag]   tried   : %s\n", gGpsDiag.tried);
  out.printf("[diag]   hint    : %s\n", gGpsDiag.hint);
  out.printf("[diag]   live    : %lu bytes, %lu NMEA ok, %lu bad-cksum, GGA=%lu RMC=%lu GSV=%lu, "
             "fixQ=%u sats=%u/%u, last %lds ago\n",
             (unsigned long)gGpsStats.bytes, (unsigned long)gGpsStats.sentences,
             (unsigned long)gGpsStats.checksumErrors, (unsigned long)gGpsStats.gga,
             (unsigned long)gGpsStats.rmc, (unsigned long)gGpsStats.gsv, gGpsStats.fixQuality,
             gCurrentFix.satellites, gGpsStats.satsInView,
             gGpsStats.lastSentenceMs ? (long)((millis() - gGpsStats.lastSentenceMs) / 1000) : -1L);
  if (gCurrentFix.valid)
    out.printf("[diag]   fix     : %.6f, %.6f alt=%.1fm hdop=%.1f\n", gCurrentFix.lat,
               gCurrentFix.lon, gCurrentFix.alt, gCurrentFix.hdop);
  if (gGpsStats.lastSentence[0]) out.printf("[diag]   last    : %s\n", gGpsStats.lastSentence);

  out.printf("[diag] CC1101 : %s", gCc1101Diag.probed ? (gCc1101Diag.found ? "FOUND" : "NOT FOUND") : "not probed");
  if (gCc1101Diag.found)
    out.printf(" CSn=%s PARTNUM=0x%02X VERSION=0x%02X GDO0=%s GDO2=%s", fyDiagPinLabel(gCc1101Diag.csPin),
               gCc1101Diag.partnum, gCc1101Diag.version, fyDiagPinLabel(gCc1101Diag.gdo0Pin),
               fyDiagPinLabel(gCc1101Diag.gdo2Pin));
  out.println();
  out.printf("[diag]   tried   : %s\n", gCc1101Diag.tried);
  out.printf("[diag]   hint    : %s\n", gCc1101Diag.hint);
  out.println("[diag] ================================");
}

static void htmlEsc(Print &out, const char *s) {
  for (; *s; s++) {
    if (*s == '<') out.print("&lt;");
    else if (*s == '>') out.print("&gt;");
    else if (*s == '&') out.print("&amp;");
    else out.print(*s);
  }
}

void fyDiagPrintHtml(Print &out) {
  const char *ok = "<b style='color:#00ff88'>DETECTED</b>";
  const char *bad = "<b style='color:#ff5566'>NOT DETECTED</b>";
  out.print("<div class='card'><h3>Modules</h3><table>");
  out.print("<tr><th>Module</th><th>Status</th><th>Details</th></tr>");

  out.print("<tr><td>GPS (AT6668)</td><td>");
  out.print(gGpsDiag.found ? ok : bad);
  out.print("</td><td>");
  if (gGpsDiag.found && gGpsDiag.transport == GPS_TRANSPORT_UART)
    out.printf("UART RX %s @ %lu<br>", fyDiagPinLabel(gGpsDiag.rxPin), (unsigned long)gGpsDiag.baud);
  if (gGpsDiag.found && gGpsDiag.transport == GPS_TRANSPORT_I2C)
    out.printf("I2C 0x%02X<br>", gGpsDiag.i2cAddr);
  out.printf("NMEA ok %lu / bad %lu, fix quality %u, sats %u used / %u in view<br>",
             (unsigned long)gGpsStats.sentences, (unsigned long)gGpsStats.checksumErrors,
             gGpsStats.fixQuality, gCurrentFix.satellites, gGpsStats.satsInView);
  if (gCurrentFix.valid) out.printf("Fix: %.6f, %.6f<br>", gCurrentFix.lat, gCurrentFix.lon);
  out.print("I2C bus: "); htmlEsc(out, gGpsDiag.i2cDevices); out.print("<br>");
  out.print("Tried: "); htmlEsc(out, gGpsDiag.tried); out.print("<br>");
  if (gGpsStats.lastSentence[0]) { out.print("Last: "); htmlEsc(out, gGpsStats.lastSentence); out.print("<br>"); }
  out.print("<i>"); htmlEsc(out, gGpsDiag.hint); out.print("</i></td></tr>");

  out.print("<tr><td>CC1101</td><td>");
  out.print(gCc1101Diag.found ? ok : bad);
  out.print("</td><td>");
  if (gCc1101Diag.found)
    out.printf("CSn %s, PARTNUM 0x%02X, VERSION 0x%02X, GDO0 %s, GDO2 %s<br>",
               fyDiagPinLabel(gCc1101Diag.csPin), gCc1101Diag.partnum, gCc1101Diag.version,
               fyDiagPinLabel(gCc1101Diag.gdo0Pin), fyDiagPinLabel(gCc1101Diag.gdo2Pin));
  out.print("Tried: "); htmlEsc(out, gCc1101Diag.tried); out.print("<br>");
  out.print("<i>"); htmlEsc(out, gCc1101Diag.hint); out.print("</i></td></tr>");
  out.print("</table><p><a class='btn' href='/modules'>JSON</a></p></div>");
}

static void jsonStr(char *buf, size_t len, const char *key, const char *val, bool comma = true) {
  appendf(buf, len, "\"%s\":\"", key);
  for (const char *p = val; *p; p++) {
    if (*p == '"' || *p == '\\') appendf(buf, len, "\\%c", *p);
    else if ((uint8_t)*p < 0x20) continue;
    else appendf(buf, len, "%c", *p);
  }
  appendf(buf, len, "\"%s", comma ? "," : "");
}

size_t fyDiagJson(char *buf, size_t len) {
  buf[0] = '\0';
  appendf(buf, len, "{\"gps\":{\"probed\":%s,\"detected\":%s,\"transport\":\"%s\",\"rx_pin\":%d,"
          "\"baud\":%lu,\"i2c_addr\":%u,\"probe_ms\":%lu,",
          gGpsDiag.probed ? "true" : "false", gGpsDiag.found ? "true" : "false",
          gGpsDiag.transport == GPS_TRANSPORT_UART ? "uart" : gGpsDiag.transport == GPS_TRANSPORT_I2C ? "i2c" : "none",
          gGpsDiag.rxPin, (unsigned long)gGpsDiag.baud, gGpsDiag.i2cAddr, (unsigned long)gGpsDiag.probeMs);
  appendf(buf, len, "\"bytes\":%lu,\"nmea_ok\":%lu,\"nmea_bad\":%lu,\"fix_quality\":%u,"
          "\"sats_used\":%u,\"sats_in_view\":%u,\"fix_valid\":%s,\"lat\":%.6f,\"lon\":%.6f,",
          (unsigned long)gGpsStats.bytes, (unsigned long)gGpsStats.sentences,
          (unsigned long)gGpsStats.checksumErrors, gGpsStats.fixQuality, gCurrentFix.satellites,
          gGpsStats.satsInView, gCurrentFix.valid ? "true" : "false", gCurrentFix.lat, gCurrentFix.lon);
  jsonStr(buf, len, "i2c_devices", gGpsDiag.i2cDevices);
  jsonStr(buf, len, "sample", gGpsDiag.sample);
  jsonStr(buf, len, "last_sentence", gGpsStats.lastSentence);
  jsonStr(buf, len, "tried", gGpsDiag.tried);
  jsonStr(buf, len, "hint", gGpsDiag.hint, false);
  appendf(buf, len, "},\"cc1101\":{\"probed\":%s,\"detected\":%s,\"cs_pin\":%d,\"gdo0_pin\":%d,"
          "\"gdo2_pin\":%d,\"partnum\":%u,\"version\":%u,\"marcstate\":%u,",
          gCc1101Diag.probed ? "true" : "false", gCc1101Diag.found ? "true" : "false",
          gCc1101Diag.csPin, gCc1101Diag.gdo0Pin, gCc1101Diag.gdo2Pin, gCc1101Diag.partnum,
          gCc1101Diag.version, gCc1101Diag.marcstate);
  jsonStr(buf, len, "tried", gCc1101Diag.tried);
  jsonStr(buf, len, "hint", gCc1101Diag.hint, false);
  appendf(buf, len, "}}");
  return strnlen(buf, len);
}
