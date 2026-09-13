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
    if (!sdPath.startsWith("/")) sdPath = String("/") + sdPath;
    return SD.open(sdPath.c_str(), mode);
  }
  // SPIFFS requires leading "/" on all paths
  String spiffsPath = path;
  if (!spiffsPath.startsWith("/")) spiffsPath = String("/") + spiffsPath;
  return SPIFFS.open(spiffsPath.c_str(), mode);
}

bool fyExists(const char *path)
{
  if (gStorageChoice == StorageChoice::Sd && gStorageReady)
  {
    String sdPath = path;
    if (!sdPath.startsWith("/")) sdPath = String("/") + sdPath;
    return SD.exists(sdPath);
  }
  // SPIFFS requires leading "/"
  String spiffsPath = path;
  if (!spiffsPath.startsWith("/")) spiffsPath = String("/") + spiffsPath;
  return SPIFFS.exists(spiffsPath);
}

bool fyRemove(const char *path)
{
  if (gStorageChoice == StorageChoice::Sd && gStorageReady)
  {
    String sdPath = path;
    if (!sdPath.startsWith("/")) sdPath = String("/") + sdPath;
    return SD.remove(sdPath);
  }
  // SPIFFS requires leading "/"
  String spiffsPath = path;
  if (!spiffsPath.startsWith("/")) spiffsPath = String("/") + spiffsPath;
  return SPIFFS.remove(spiffsPath);
}

bool fyRename(const char *src, const char *dst)
{
  if (gStorageChoice == StorageChoice::Sd && gStorageReady)
  {
    String s = String(src);
    if (!s.startsWith("/")) s = String("/") + s;
    String d = String(dst);
    if (!d.startsWith("/")) d = String("/") + d;
    return SD.rename(s, d);
  }
  // SPIFFS requires leading "/"
  String spiffsSrc = String(src);
  if (!spiffsSrc.startsWith("/")) spiffsSrc = String("/") + spiffsSrc;
  String spiffsDst = String(dst);
  if (!spiffsDst.startsWith("/")) spiffsDst = String("/") + spiffsDst;
  return SPIFFS.rename(spiffsSrc.c_str(), spiffsDst.c_str());
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
