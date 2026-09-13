// storage_select.cpp — SD/SPIFFS boot menu for flock-you-esp32
#include "storage_select.h"
#include "m5basic_display.h"
#include <SPI.h>
#include <SD.h>

static constexpr int SD_CS_PIN = 4;
static constexpr unsigned long HOLD_MS = 2000;

static void drawMenu(const char *title, const char *optA, const char *optB)
{
  M5.Display.fillScreen(MB_BLACK);
  M5.Display.setTextColor(MB_WHITE);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(10, 20);
  M5.Display.println(title);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(10, 80);
  M5.Display.print("[A] ");
  M5.Display.println(optA);
  M5.Display.setCursor(10, 110);
  M5.Display.print("[B] ");
  M5.Display.println(optB);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(10, 220);
  M5.Display.println("A=Left  B=Right");
}

// Returns true if user picks optB (right), false if optA (left)
static bool waitSelect(const char *title, const char *optA, const char *optB,
                       bool defaultB)
{
  drawMenu(title, optA, optB);
  // Wait for any held button to be released first
  while (M5.BtnA.isPressed() || M5.BtnB.isPressed())
  {
    M5.update();
    delay(5);
  }
  unsigned long start = millis();
  while (true)
  {
    M5.update();
    if (M5.BtnA.wasPressed())
      return false; // Left = optA = No
    if (M5.BtnB.wasPressed())
      return true; // Right = optB = Yes
    if (millis() - start > 5000)
      return defaultB; // Auto-select after 5 seconds timeout
    delay(10);
  }
}

static bool confirm(const char *msg)
{
  // Wait for any held button to be released first
  while (M5.BtnA.isPressed() || M5.BtnB.isPressed())
  {
    M5.update();
    delay(5);
  }
  while (true)
  {
    M5.Display.fillScreen(MB_BLACK);
    M5.Display.setTextColor(MB_WHITE);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(10, 70);
    M5.Display.println(msg);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(10, 180);
    M5.Display.print("[A] No");
    M5.Display.setCursor(130, 180);
    M5.Display.print("[B] Yes");
    M5.update();
    if (M5.BtnA.wasPressed())
      return false; // Left = No
    if (M5.BtnB.wasPressed())
      return true; // Right = Yes
    delay(10);
  }
}

void notify(const char *msg)
{
  M5.Display.fillScreen(MB_BLACK);
  M5.Display.setTextColor(MB_GREEN);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(10, 100);
  M5.Display.println(msg);
  unsigned long start = millis();
  while (millis() - start < HOLD_MS)
  {
    M5.update();
    if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed())
      break;
    delay(10);
  }
}

static bool sdPresent()
{
  // On M5Stack Basic, the SD card connects via SPI:
  //   CLK → GPIO14 (SPI_CLK)   — NOT GPIO2
  //   MISO → GPIO19            — NOT GPIO2
  //   MOSI → GPIO23            — NOT GPIO2
  //   CS → GPIO4
  // There is NO dedicated card-detect pin on M5Stack Basic — GPIO2 is
  // part of the SPI bus (or I2S on some variants), NOT a detect switch.
  // The old code checking GPIO2 as card-detect was reading the CLK line
  // and incorrectly reporting "no card" when the pin was HIGH.
  SPI.begin();
  return SD.begin(SD_CS_PIN, SPI, 25000000);
}

StorageResult storageBootMenu()
{
  StorageResult r{StorageChoice::Spiffs, false, false};

  if (!sdPresent())
  {
    notify("No SD detected\n\nUsing SPIFFS");
    return r;
  }

  r.sdMounted = true;

  if (!waitSelect("Storage?", "SPIFFS", "SD Card", true))
  {
    notify("Using SPIFFS");
    return r;
  }

  r.choice = StorageChoice::Sd;
  return r;
}
