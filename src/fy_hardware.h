// flock-you-esp32 — Hardware detection for LoRa (GPS/CC1101: fy_module_diag.cpp)
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

// GPS and CC1101 detection moved to fy_module_diag.cpp: the M5Stack GPS
// Module v2.1 is UART (not I2C), and the CC1101's CSn/GDO pins are DIP-switch
// selectable, so both need a multi-pin probe rather than a single fixed check.

#endif /* FY_HARDWARE_H */
