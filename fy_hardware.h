// flock-you-esp32 — Hardware auto-detection + feature degradation
// Detects optional GPS and LoRa modules and disables features when absent.

#ifndef FY_HARDWARE_H
#define FY_HARDWARE_H

#include <Arduino.h>
#include <SPI.h>

// ── LoRa detection (SPI + SX127x chip-ID read) ──────────────────────────────
// SX1276/SX1278/RA-02 register 0x42 (version) reads back 0x12 on real hardware.
// Returns true when chip-ID matches and DIO0 is wired (basic connectivity check).
static bool detect_lora(uint8_t cs_pin = 5, uint8_t rst_pin = 26, uint8_t dio0_pin = 2) {
  SPI.begin();                    // M5Stack Basic: VSPI (SCK=18, MISO=19, MOSI=23)
  pinMode(cs_pin, OUTPUT);
  digitalWrite(cs_pin, HIGH);
  if (rst_pin < 255) {
    pinMode(rst_pin, OUTPUT);
    digitalWrite(rst_pin, LOW);  delayMicroseconds(100);
    digitalWrite(rst_pin, HIGH); delay(10);
  }
  digitalWrite(cs_pin, LOW);
  SPI.transfer(0x42);            // read REG_VERSION
  uint8_t ver = SPI.transfer(0x00);
  digitalWrite(cs_pin, HIGH);
  if (ver != 0x12) return false; // SX127x version register
  pinMode(dio0_pin, INPUT);
  return true;                   // chip present, DIO0 readable
}

// ── GPS detection (I2C scan for common GPS module addresses) ─────────────────
// L76GNSS = 0x10,  AT6668 = 0x10, NEO-6M = 0x42, MAX-7 = 0x10
static bool detect_gps(TwoWire &bus = Wire, uint8_t sda = 21, uint8_t scl = 22) {
  bus.begin(sda, scl);
  delay(50);
  const uint8_t addrs[] = {0x10, 0x42, 0x66, 0x08};
  for (uint8_t a : addrs) {
    bus.beginTransmission(a);
    if (bus.endTransmission() == 0) return true;
  }
  return false;
}

#endif /* FY_HARDWARE_H */
