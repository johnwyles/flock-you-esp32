// storage_backend.h — unified SPIFFS/SD file API for flock-you-esp32
#pragma once

#include <SPIFFS.h>
#include <SD.h>
#include "storage_select.h"

extern StorageChoice gStorageChoice;
extern bool gStorageReady;
extern SPIClass gSdSpi;

File fyOpen(const char* path, const char* mode);
bool fyExists(const char* path);
bool fyRemove(const char* path);
bool fyRename(const char* src, const char* dst);
bool fyInitStorage(StorageResult res);
bool fyTestSdWrite(); // test write on SD; returns true if writable
const char* fyStorageLabel();
