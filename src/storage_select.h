// storage_select.h — SD/SPIFFS boot menu for flock-you-esp32
#pragma once

#include <M5Unified.h>
#include <SPIFFS.h>
#include <SD.h>

enum class StorageChoice : uint8_t { None, Spiffs, Sd };

struct StorageResult {
  StorageChoice choice;
  bool sdFormatted;
  bool sdMounted;
};

StorageResult storageBootMenu();
void notify(const char* msg);
