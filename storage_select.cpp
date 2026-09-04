// storage_select.cpp — SD/SPIFFS boot menu for flock-you-esp32
#include "storage_select.h"
#include "m5basic_display.h"
#include <SPI.h>
#include <SD.h>

static constexpr int SD_CS_PIN = 4;
static constexpr unsigned long HOLD_MS = 2000;

static void drawMenu(const char* title, const char* optA, const char* optB) {
  M5.Display.fillScreen(MB_BLACK);
  M5.Display.setTextColor(MB_WHITE);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(10, 30);
  M5.Display.println(title);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(10, 90);
  M5.Display.print("[A] ");
  M5.Display.println(optA);
  M5.Display.setCursor(10, 120);
  M5.Display.print("[B] ");
  M5.Display.println(optB);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(10, 220);
  M5.Display.println("A=Left  B=Right  C=Select");
}

static bool waitSelect(const char* title, const char* optA, const char* optB,
                       bool defaultA) {
  drawMenu(title, optA, optB);
  while (true) {
    M5.update();
    if (M5.BtnA.wasPressed()) return defaultA;
    if (M5.BtnB.wasPressed()) return !defaultA;
    if (M5.BtnC.wasPressed()) return defaultA;  // C confirms current default
    delay(10);
  }
}

static bool confirm(const char* msg) {
  while (true) {
    M5.Display.fillScreen(MB_BLACK);
    M5.Display.setTextColor(MB_WHITE);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(10, 80);
    M5.Display.println(msg);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(10, 120);
    M5.Display.println("[A] No  [B] Yes  [C] No");
    M5.update();
    if (M5.BtnA.wasPressed()) return false;
    if (M5.BtnB.wasPressed()) return true;
    if (M5.BtnC.wasPressed()) return false;
    delay(10);
  }
}

static void notify(const char* msg) {
  M5.Display.fillScreen(MB_BLACK);
  M5.Display.setTextColor(MB_GREEN);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(10, 100);
  M5.Display.println(msg);
  unsigned long start = millis();
  while (millis() - start < HOLD_MS) {
    M5.update();
    if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed()) break;
    delay(10);
  }
}

static bool sdPresent() {
  SPI.begin();
  return SD.begin(SD_CS_PIN, SPI, 25000000);
}


StorageResult storageBootMenu() {
  StorageResult r{StorageChoice::Spiffs, false, false};

  if (!sdPresent()) {
    notify("No SD detected\nUsing SPIFFS");
    return r;
  }

  r.sdMounted = true;
  if (!waitSelect("Storage?", "SD Card", "SPIFFS", false)) {
    notify("Using SPIFFS");
    return r;
  }

  if (!waitSelect("Format SD?", "No", "Yes", false)) {
    notify("Using SPIFFS");
    return r;
  }

  if (!confirm("Are you sure?")) {
    notify("Using SPIFFS");
    return r;
  }

  r.choice = StorageChoice::Sd;
  r.sdFormatted = false;
  notify("Using SD Card");
  return r;
}
