// flock-you-esp32 — CC1101 sub-GHz support
// M5Stack Module CC1101 (M-Bus stacking module, SPI on G18/G19/G23;
// CSn/GDO pins chosen by the module's DIP switches — see fy_module_diag.cpp)

#include "fy_cc1101.h"

extern SubGHzDetection gSubGHzDet;
extern bool gHasCC1101;

// CC1101 frequency configuration for each band
static const struct {
  uint8_t freq2, freq1, freq0;
  uint8_t mdmcfg4, mdmcfg3, mdmcfg2, mdmcfg1, mdmcfg0;
  uint8_t deviatn;
} bandConfig[] = {
  // 315 MHz, 2.4 kbps, FSK, 5 kHz deviation
  {0x0C, 0x00, 0x00, 0x07, 0x83, 0x22, 0x00, 0xF8, 0x15},
  // 433 MHz, 2.4 kbps, FSK, 5 kHz deviation
  {0x10, 0x00, 0x00, 0x07, 0x83, 0x22, 0x00, 0xF8, 0x15},
  // 868 MHz, 2.4 kbps, FSK, 5 kHz deviation
  {0x21, 0x00, 0x00, 0x07, 0x83, 0x22, 0x00, 0xF8, 0x15},
  // 915 MHz, 2.4 kbps, FSK, 5 kHz deviation
  {0x23, 0x00, 0x00, 0x07, 0x83, 0x22, 0x00, 0xF8, 0x15}
};

static SPIClass *gSpi = nullptr;
static uint8_t gCs = CC1101_CS;
static uint8_t gCurrentBand = CC1101_BAND_433;
static bool gDetected = false;
static const SPISettings kCcSpi(1000000, MSBFIRST, SPI_MODE0);

// SPI transfer helpers
static uint8_t spiTransfer(uint8_t val) {
  return gSpi->transfer(val);
}

// Wait for CHIP_RDYn (SO/MISO goes low after CSn is pulled low).
static bool cc1101WaitMisoLow(uint32_t timeoutUs = 2000) {
  uint32_t start = micros();
  while (digitalRead(CC1101_MISO)) {
    if (micros() - start > timeoutUs) return false;
  }
  return true;
}

static void csLow()  { gSpi->beginTransaction(kCcSpi); digitalWrite(gCs, LOW); cc1101WaitMisoLow(); }
static void csHigh() { digitalWrite(gCs, HIGH); gSpi->endTransaction(); }

// Registers 0x30-0x3D are STATUS registers: they are only reachable with the
// burst bit set (0xC0). A plain read (0x80) of 0x30-0x3D is interpreted as a
// COMMAND STROBE (e.g. 0xB0 = SRES), which is why PARTNUM/VERSION never
// read back correctly before.
static uint8_t cc1101ReadReg(uint8_t reg) {
  uint8_t hdr = (reg >= 0x30 && reg <= 0x3D) ? (reg | 0xC0) : (reg | 0x80);
  csLow();
  spiTransfer(hdr);
  uint8_t val = spiTransfer(0x00);
  csHigh();
  return val;
}

static void cc1101WriteReg(uint8_t reg, uint8_t val) {
  csLow();
  spiTransfer(reg);
  spiTransfer(val);
  csHigh();
}

static void cc1101WriteStrobe(uint8_t cmd) {
  csLow();
  spiTransfer(cmd);
  csHigh();
}

// Presence check: PARTNUM must be 0x00, and a write/read-back of a
// read/write config register (SYNC1) must round-trip. VERSION is reported
// but not required to be 0x14 (genuine parts read 0x14, some clones 0x04/0x17).
bool cc1101Detect(SPIClass &spi, uint8_t cs) {
  gSpi = &spi;
  gCs = cs;
  digitalWrite(gCs, HIGH);
  pinMode(gCs, OUTPUT);
  delay(1);
  uint8_t partnum = cc1101ReadReg(CC1101_PARTNUM);
  uint8_t version = cc1101ReadReg(CC1101_VERSION);
  uint8_t saved = cc1101ReadReg(CC1101_SYNC1);
  cc1101WriteReg(CC1101_SYNC1, 0xA5);
  uint8_t rb1 = cc1101ReadReg(CC1101_SYNC1);
  cc1101WriteReg(CC1101_SYNC1, 0x5A);
  uint8_t rb2 = cc1101ReadReg(CC1101_SYNC1);
  cc1101WriteReg(CC1101_SYNC1, saved);
  bool ok = (partnum == 0x00) && rb1 == 0xA5 && rb2 == 0x5A &&
            version != 0x00 && version != 0xFF;
  Serial.printf("[cc1101] detect CS=G%u: PARTNUM=0x%02X VERSION=0x%02X "
                "SYNC1 rw=0x%02X/0x%02X -> %s\r\n",
                cs, partnum, version, rb1, rb2, ok ? "PRESENT" : "not found");
  return ok;
}

void cc1101Init(SPIClass &spi, uint8_t cs) {
  gSpi = &spi;
  gCs = cs;
  digitalWrite(gCs, HIGH);
  pinMode(gCs, OUTPUT);
  delay(1);
  // Reset
  cc1101WriteStrobe(CC1101_SRES);
  delay(10);
  gDetected = cc1101Detect(spi, cs);
  if (!gDetected) return;
  // Initialize with 433 MHz default
  cc1101SetBand(CC1101_BAND_433);
  cc1101Rx();
}

void cc1101SetBand(uint8_t band) {
  if (band >= sizeof(bandConfig) / sizeof(bandConfig[0])) return;
  gCurrentBand = band;
  const auto &cfg = bandConfig[band];
  cc1101WriteReg(CC1101_FREQ2, cfg.freq2);
  cc1101WriteReg(CC1101_FREQ1, cfg.freq1);
  cc1101WriteReg(CC1101_FREQ0, cfg.freq0);
  cc1101WriteReg(CC1101_MDMCFG4, cfg.mdmcfg4);
  cc1101WriteReg(CC1101_MDMCFG3, cfg.mdmcfg3);
  cc1101WriteReg(CC1101_MDMCFG2, cfg.mdmcfg2);
  cc1101WriteReg(CC1101_MDMCFG1, cfg.mdmcfg1);
  cc1101WriteReg(CC1101_MDMCFG0, cfg.mdmcfg0);
  cc1101WriteReg(CC1101_DEVIATN, cfg.deviatn);
}

void cc1101Rx() {
  cc1101WriteStrobe(CC1101_SIDLE);
  delay(1);
  cc1101WriteStrobe(CC1101_SFRX);
  delay(1);
  cc1101WriteStrobe(CC1101_SRX);
}

bool cc1101ReadPacket(SubGHzDetection &det) {
  if (!gDetected) return false;
  uint8_t rxbytes = cc1101ReadReg(CC1101_RXBYTES) & 0x7F;  // bit7 = overflow
  if (rxbytes == 0) return false;
  // Read FIFO
  csLow();
  spiTransfer(0x3F | 0xC0); // burst read RX FIFO
  det.length = spiTransfer(0x00);
  if (det.length > 64) det.length = 64;
  for (int i = 0; i < det.length; i++) {
    det.data[i] = spiTransfer(0x00);
  }
  csHigh();
  {
    // RSSI register is two's complement, 0.5 dB steps, 74 dB offset (datasheet 17.3)
    int raw = (int8_t)cc1101ReadReg(CC1101_RSSI);
    det.rssi = (int8_t)(raw / 2 - 74);
  }
  det.timestampMs = millis();
  det.band = gCurrentBand;
  det.sigType = cc1101Classify(det.data, det.length, det.band);
  det.frequency = (gCurrentBand == CC1101_BAND_315) ? 315000000 :
                  (gCurrentBand == CC1101_BAND_433) ? 433920000 :
                  (gCurrentBand == CC1101_BAND_868) ? 868300000 : 915000000;
  // Flush RX FIFO
  cc1101WriteStrobe(CC1101_SFRX);
  cc1101Rx();
  return true;
}

uint8_t cc1101Classify(const uint8_t *data, uint8_t len, uint8_t band) {
  if (len < 2) return CC1101_SIG_UNKNOWN;
  // Simple heuristics for common sub-GHz protocols
  if (band == CC1101_BAND_433 && len >= 8) {
    // TPMS typically has 8-12 byte packets with specific preamble
    if (data[0] == 0x55 && data[1] == 0xAA) return CC1101_SIG_TPMS;
    // Weather stations often start with 0x24 or 0x25
    if (data[0] == 0x24 || data[0] == 0x25) return CC1101_SIG_WEATHER;
    // Generic 433 MHz device
    return CC1101_SIG_REMOTE;
  }
  if (band == CC1101_BAND_315 && len >= 3) {
    // Car remotes typically have 3-6 byte packets
    if (len <= 6) return CC1101_SIG_REMOTE;
  }
  return CC1101_SIG_UNKNOWN;
}

void cc1101Scan() {
  if (!gDetected) return;
  for (int band = 0; band < 4; band++) {
    cc1101SetBand(band);
    delay(30);
    cc1101Rx();
    delay(80);
    SubGHzDetection det;
    if (cc1101ReadPacket(det)) {
      gSubGHzDet = det;
      const char *typeStr = "unknown";
      switch (det.sigType) {
        case CC1101_SIG_TPMS:   typeStr = "TPMS"; break;
        case CC1101_SIG_REMOTE: typeStr = "remote"; break;
        case CC1101_SIG_WEATHER: typeStr = "weather"; break;
        case CC1101_SIG_GARAGE: typeStr = "garage"; break;
        default: break;
      }
      cc1101AddDetection(det);
      Serial.printf("[cc1101] %s on %u MHz, RSSI=%d dBm, len=%d\r\n",
                     typeStr, (unsigned)(det.frequency / 1000000), det.rssi, det.length);
    }
  }
}

void cc1101AppendToJSON(char *buf, size_t len) {
  if (!gHasCC1101) {
    snprintf(buf, len, ",\"subghz\":null");
    return;
  }
  const char *typeStr = "unknown";
  switch (gSubGHzDet.sigType) {
    case CC1101_SIG_TPMS:   typeStr = "tpms"; break;
    case CC1101_SIG_REMOTE: typeStr = "remote"; break;
    case CC1101_SIG_WEATHER: typeStr = "weather"; break;
    case CC1101_SIG_GARAGE: typeStr = "garage"; break;
    default: break;
  }
  snprintf(buf, len, ",\"subghz\":{\"freq_mhz\":%u,\"rssi\":%d,"
                     "\"band\":%d,\"type\":\"%s\",\"len\":%d}",
           (unsigned)(gSubGHzDet.frequency / 1000000), gSubGHzDet.rssi,
           gSubGHzDet.band, typeStr, gSubGHzDet.length);
}




bool cc1101AddDetectionFake(uint8_t sigType, uint32_t freqHz, int8_t rssi) {
  SubGHzDetection det;
  memset(&det, 0, sizeof(det));
  det.frequency = freqHz;
  det.rssi = rssi;
  det.band = (freqHz >= 868000000) ? CC1101_BAND_868 : (freqHz >= 433000000) ? CC1101_BAND_433 : (freqHz >= 315000000) ? CC1101_BAND_315 : CC1101_BAND_915;
  det.length = 8;
  det.sigType = sigType;
  det.timestampMs = millis();
  return cc1101AddDetection(det);
}
