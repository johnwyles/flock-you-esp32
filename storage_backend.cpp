// storage_backend.cpp — unified SD/SPIFFS file API
#include "storage_backend.h"

StorageChoice gStorageChoice = StorageChoice::Spiffs;
bool gStorageReady = false;
SPIClass gSdSpi = SPIClass(HSPI);

File fyOpen(const char *path, const char *mode)
{
  if (gStorageChoice == StorageChoice::Sd && gStorageReady)
  {
    String sdPath = path;
    if (sdPath.startsWith("/")) sdPath.remove(0,1);
    return SD.open(sdPath.c_str(), mode);
  }
  return SPIFFS.open(path, mode);
}

bool fyExists(const char *path)
{
  if (gStorageChoice == StorageChoice::Sd && gStorageReady)
  {
    String sdPath = path;
    if (sdPath.startsWith("/")) sdPath.remove(0,1);
    return SD.exists(sdPath);
  }
  return SPIFFS.exists(path);
}

bool fyRemove(const char *path)
{
  if (gStorageChoice == StorageChoice::Sd && gStorageReady)
  {
    String sdPath = path;
    if (sdPath.startsWith("/")) sdPath.remove(0,1);
    return SD.remove(sdPath);
  }
  return SPIFFS.remove(path);
}

bool fyRename(const char *src, const char *dst)
{
  if (gStorageChoice == StorageChoice::Sd && gStorageReady)
  {
    String s = String(src);
    if (s.startsWith("/")) s.remove(0,1);
    String d = String(dst);
    if (d.startsWith("/")) d.remove(0,1);
    return SD.rename(s, d);
  }
  return SPIFFS.rename(src, dst);
}

bool fyInitStorage(StorageResult res)
{
  gStorageChoice = res.choice;
  if (gStorageChoice == StorageChoice::Sd && res.sdMounted)
  {
    SPI.begin();
    gStorageReady = SD.begin(4, gSdSpi, 25000000);
    return gStorageReady;
  }
  gStorageReady = SPIFFS.begin(true);
  return gStorageReady;
}

const char *fyStorageLabel()
{
  if (gStorageChoice == StorageChoice::Sd && gStorageReady)
    return "SD";
  return "SPIFFS";
}

bool fyTestSdWrite()
{
  if (gStorageChoice != StorageChoice::Sd)
    return false;
  String testPath = "/.fw_test";
  File f = SD.open(testPath, FILE_WRITE);
  if (!f)
    return false;
  f.write((const uint8_t *)"TEST", 4);
  f.close();
  if (!SD.exists(testPath))
    return false;
  if (!SD.remove(testPath))
    return false;
  return true;
}
