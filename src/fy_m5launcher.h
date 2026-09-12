// flock-you-esp32 — M5Launcher integration
// Build firmware that M5Launcher can load from SD card

#ifndef FY_M5LAUNCHER_H
#define FY_M5LAUNCHER_H

#include <Arduino.h>

// M5Launcher requires firmwares to be built as .bin files placed on SD card
// in a specific folder structure. The launcher handles partitioning and booting.

// Firmware metadata for M5Launcher catalog (optional, for web catalog)
#define FY_LAUNCHER_NAME "flock-you"
#define FY_LAUNCHER_VERSION "1.0.0"
#define FY_LAUNCHER_AUTHOR "johnwyles"
#define FY_LAUNCHER_DESC "Passive Flock Safety ALPR detector with optional GPS/LoRa"

// Build-time flag: FY_M5LAUNCHER=1 enables M5Launcher-compatible build
// - Removes partition table from firmware (M5Launcher provides its own)
// - Sets correct flash offset (0x10000 for app partition)
// - Outputs clean .bin for SD card copy

#endif /* FY_M5LAUNCHER_H */
