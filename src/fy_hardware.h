// flock-you-esp32 — Hardware detection for GPS, LoRa, CC1101
// Disables features automatically when modules are absent.

#ifndef FY_HARDWARE_H
#define FY_HARDWARE_H

#include <Arduino.h>
#include <SPI.h>

// ── LoRa detection (SPI + SX127x chip-ID read) ───────────────────────────────
// SX1276/SX1278/RA-02 register 0x42 (version) reads back 0x12 on real hardware.
static bool detect_lora(uint8_t cs_pin = 5, uint8_t rst_pin = 26, uint8_t dio0_pin = 2) {
  SPI.begin();
  pinMode(cs_pin, OUTPUT);
  digitalWrite(cs_pin, HIGH);
  if (rst_pin != 0xFF) {
    pinMode(rst_pin, OUTPUT);
    digitalWrite(rst_pin, LOW);
    delay(10);
    digitalWrite(rst_pin, HIGH);
    delay(10);
  }
  digitalWrite(cs_pin, LOW);
  SPI.transfer(0x42);
  uint8_t ver = SPI.transfer(0x00);
  digitalWrite(cs_pin, HIGH);
  return (ver == 0x12);
}

// ── CC1101 detection (SPI + PARTNUM read) ────────────────────────────────────
// CC1101 PARTNUM register (0x30) reads back 0x00 on real hardware.
static bool detect_cc1101(SPIClass &spi = SPI, uint8_t cs = 4) {
  spi.begin();
  pinMode(cs, OUTPUT);
  digitalWrite(cs, HIGH);
  delay(1);
  digitalWrite(cs, LOW);
  spi.transfer(0x30 | 0x80);
  uint8_t part = spi.transfer(0x00);
  digitalWrite(cs, HIGH);
  return (part == 0x00);
}

// ── GPS detection (I2C scan) ─────────────────────────────────────────────────
static bool detect_gps(TwoWire &bus = Wire, uint8_t sda = 21, uint8_t scl = 22) {
  bus.begin(sda, scl);
  delay(10);
  const uint8_t addrs[] = {0x10, 0x42, 0x66, 0x08};
  for (uint8_t i = 0; i < sizeof(addrs); i++) {
    bus.beginTransmission(addrs[i]);
    if (bus.endTransmission() == 0) return true;
  }
  return false;
}

#endif /* FY_HARDWARE_H */
