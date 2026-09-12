// fy_serial.cpp — Serial debug command parser for flock-you-esp32
//
// Extracted from main.cpp's loop() serial-command handling.
// Commands:
//   CMD:HELP     — list available commands
//   CMD:INFO     — print device info (model, flash, heap, WiFi channel)
//   CMD:STATUS   — brief runtime status (detections, module presence)
//   CMD:WEB      — toggle web server on/off
//   CMD:FAKE     — inject one detection of each active module type
//   CMD:FAKE_WIFI / CMD:FAKE_BLE / CMD:FAKE_TPMS / CMD:FAKE_REMOTE /
//   CMD:FAKE_WEATHER / CMD:FAKE_GPS / CMD:FAKE_LORA
//   CMD:CLEAR    — clear all detections
//   CMD:SAVE     — manually persist session

#include <Arduino.h>
#include "fy_globals.h"
#include "fy_gps.h"
#include "fy_cc1101.h"
#include "fy_webserver.h"
#include "fy_serial.h"

// Forward declarations for functions defined in main.cpp / other modules
extern bool loraAddDetectionFake(uint32_t freqHz, int8_t rssi);

void fySerialProcess()
{
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  if (cmd.equalsIgnoreCase("CMD:HELP")) {
    Serial.println("[flockyou] Commands:");
    Serial.println("  CMD:HELP     — this help");
    Serial.println("  CMD:INFO     — device info (chip, flash, heap, wifi)");
    Serial.println("  CMD:STATUS   — runtime status summary");
    Serial.println("  CMD:WEB      — toggle web server on/off");
    Serial.println("  CMD:FAKE     — inject detections from all active modules");
    Serial.println("  CMD:FAKE_WIFI — inject fake WiFi OUI detection");
    Serial.println("  CMD:FAKE_BLE  — inject fake BLE detection");
    Serial.println("  CMD:FAKE_TPMS — inject fake CC1101 TPMS detection");
    Serial.println("  CMD:FAKE_REMOTE — inject fake CC1101 remote detection");
    Serial.println("  CMD:FAKE_WEATHER — inject fake CC1101 weather detection");
    Serial.println("  CMD:FAKE_GPS  — inject fake GPS waypoint");
    Serial.println("  CMD:FAKE_LORA — inject fake LoRa detection");
    Serial.println("  CMD:CLEAR    — clear all detections");
    Serial.println("  CMD:SAVE     — manually save session now");
    Serial.println("  CMD:DEBUG    — toggle debug verbosity");

  } else if (cmd.equalsIgnoreCase("CMD:INFO")) {
    Serial.println("[flockyou] === Device Info ===");
#if defined(USE_M5BASIC)
    Serial.println("[flockyou] Board: M5Stack Basic / Core2 For AWS");
#elif defined(USE_M5STICKC_PLUS_SE)
    Serial.println("[flockyou] Board: M5StickC Plus SE");
#elif defined(USE_M5ATOM_LITE)
    Serial.println("[flockyou] Board: M5Atom Lite");
#elif defined(USE_M5ATOM_ECHO)
    Serial.println("[flockyou] Board: M5Atom Echo");
#elif defined(USE_M5ATOM_VOICE)
    Serial.println("[flockyou] Board: M5Atom Voice");
#elif defined(USE_M5ATOM_VOICES3R)
    Serial.println("[flockyou] Board: M5Atom VoiceS3R");
#elif defined(USE_LILYGO_T_DONGLE_C5)
    Serial.println("[flockyou] Board: LILYGO T-Dongle C5");
#else
    Serial.println("[flockyou] Board: generic ESP32");
#endif
    Serial.printf("[flockyou] CPU freq: %d MHz\n", (int)ESP.getCpuFreqMHz());
    Serial.printf("[flockyou] Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("[flockyou] Flash size: %d MB\n", (int)(ESP.getFlashChipSize() / (1024 * 1024)));
    Serial.printf("[flockyou] WiFi channel: %d\n", currentChannel);
    Serial.printf("[flockyou] Scan mode: %s\n", channelModeName());
    Serial.printf("[flockyou] Web server: %s\n", fyWebServerActive() ? "ON" : "OFF");

  } else if (cmd.equalsIgnoreCase("CMD:STATUS")) {
    Serial.printf("[flockyou] Detections: %d/%d\n", fyDetCount, MAX_DETECTIONS);
    Serial.printf("[flockyou] GPS: %s, LoRa: %s, CC1101: %s\n",
                  gHasGPS ? "yes" : "no",
                  gHasLoRa ? "yes" : "no",
                  gHasCC1101 ? "yes" : "no");
#if defined(ENABLE_BLE_SCAN) && ENABLE_BLE_SCAN
    Serial.println("[flockyou] BLE: enabled");
#else
    Serial.println("[flockyou] BLE: disabled");
#endif
    Serial.printf("[flockyou] Free heap: %d bytes\n", ESP.getFreeHeap());

  } else if (cmd.equalsIgnoreCase("CMD:WEB")) {
    if (gWebServerMode) {
      fyWebServerStop();
      gWebServerMode = false;
      Serial.println("[flockyou] Web server stopped");
    } else {
      fyWebServerStart();
      gWebServerMode = true;
      Serial.println("[flockyou] Web server started");
    }

  } else if (cmd.equalsIgnoreCase("CMD:FAKE")) {
    channelLockActive = false;
    static uint8_t fakeMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    fakeMac[5]++;
    enqueueAlert(ALERT_OUI_ADDR2, fakeMac, -45, 1, nullptr, "test", 75);
    Serial.println("[flockyou] Fake WiFi detection injected");
    if (gHasGPS) {
      waypointRecord("fake_gps");
      Serial.println("[flockyou] Fake GPS waypoint injected");
    }
    if (gHasCC1101) {
      cc1101AddDetectionFake(CC1101_SIG_TPMS, 433920, -50);
      cc1101AddDetectionFake(CC1101_SIG_REMOTE, 315000, -60);
      cc1101AddDetectionFake(CC1101_SIG_WEATHER, 433500, -55);
      Serial.println("[flockyou] Fake CC1101 detections injected");
    }
    if (gHasLoRa) {
      loraAddDetectionFake(915000, -65);
      Serial.println("[flockyou] Fake LoRa detection injected");
    }
#if defined(ENABLE_BLE_SCAN) && ENABLE_BLE_SCAN
    extern void bleInjectFake();
    bleInjectFake();
    Serial.println("[flockyou] Fake BLE detection injected");
#endif

  } else if (cmd.equalsIgnoreCase("CMD:FAKE_WIFI")) {
    channelLockActive = false;
    static uint8_t fakeMacW[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    fakeMacW[5]++;
    enqueueAlert(ALERT_OUI_ADDR2, fakeMacW, -45, 1, nullptr, "test", 75);
    Serial.println("[flockyou] Fake WiFi detection injected");

  } else if (cmd.equalsIgnoreCase("CMD:FAKE_BLE")) {
#if defined(ENABLE_BLE_SCAN) && ENABLE_BLE_SCAN
    extern void bleInjectFake();
    bleInjectFake();
    Serial.println("[flockyou] Fake BLE detection injected");
#else
    Serial.println("[flockyou] BLE not enabled");
#endif

  } else if (cmd.equalsIgnoreCase("CMD:FAKE_TPMS")) {
    if (gHasCC1101) {
      cc1101AddDetectionFake(CC1101_SIG_TPMS, 433920, -50);
      Serial.println("[flockyou] Fake TPMS detection injected");
    } else {
      Serial.println("[flockyou] CC1101 not present");
    }

  } else if (cmd.equalsIgnoreCase("CMD:FAKE_REMOTE")) {
    if (gHasCC1101) {
      cc1101AddDetectionFake(CC1101_SIG_REMOTE, 315000, -60);
      Serial.println("[flockyou] Fake remote detection injected");
    } else {
      Serial.println("[flockyou] CC1101 not present");
    }

  } else if (cmd.equalsIgnoreCase("CMD:FAKE_WEATHER")) {
    if (gHasCC1101) {
      cc1101AddDetectionFake(CC1101_SIG_WEATHER, 433500, -55);
      Serial.println("[flockyou] Fake weather detection injected");
    } else {
      Serial.println("[flockyou] CC1101 not present");
    }

  } else if (cmd.equalsIgnoreCase("CMD:FAKE_GPS")) {
    if (gHasGPS) {
      waypointRecord("fake_gps");
      Serial.println("[flockyou] Fake GPS waypoint injected");
    } else {
      Serial.println("[flockyou] GPS not present");
    }

  } else if (cmd.equalsIgnoreCase("CMD:FAKE_LORA")) {
    if (gHasLoRa) {
      loraAddDetectionFake(915000, -65);
      Serial.println("[flockyou] Fake LoRa detection injected");
    } else {
      Serial.println("[flockyou] LoRa not present");
    }

  } else if (cmd.equalsIgnoreCase("CMD:CLEAR")) {
    fyDetCount = 0;
    fyDirty = false;
    Serial.println("[flockyou] All detections cleared");

  } else if (cmd.equalsIgnoreCase("CMD:SAVE")) {
    fySaveSession();
    Serial.println("[flockyou] Session saved manually");

  } else if (cmd.equalsIgnoreCase("CMD:DEBUG")) {
    gDebugLevel = (gDebugLevel + 1) % 3;
    Serial.printf("[flockyou] Debug level: %d (0=off, 1=normal, 2=verbose)\n", gDebugLevel);

  } else if (!cmd.isEmpty()) {
    Serial.printf("[flockyou] Unknown command: %s (type CMD:HELP for list)\n", cmd.c_str());
  }
}
