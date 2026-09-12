// flock-you-esp32 — Simple web server for detection data
// Serves JSON detections + status when toggled from promiscuous mode

#include "fy_webserver.h"
#include "fy_webserver_config.h"
#include "esp_wifi.h"
#include "fy_globals.h"

extern bool mb_showWebLog;
extern char mb_webLog[120];
extern unsigned long mb_webLogMs;
extern const char *mb_wifiStatus;
#include "storage_backend.h"

// ── Globals from main.cpp (now a separate TU) ─────────────────────────────────
extern bool fySpiffsReady;
extern int fyDetCount;

static bool gWebServerActive = false;
static IPAddress gApIP(192, 168, 4, 1);
static const char *gApSSID = "flock-you";
static const char *gApPass = "flockyou";

WebServer gWebServer(80);

void fyWebServerStart() {
  if (gWebServerActive) return;
  char ssid[64] = {0};
  char pass[64] = {0};
  fyWsReadConfig(ssid, sizeof(ssid), pass, sizeof(pass));

  // Stop promiscuous mode before switching to AP mode
  esp_wifi_set_promiscuous(false);
  delay(100);

  // Use ESP-IDF API directly for clean WiFi restart — the Arduino WiFi
  // library was not used for initial WiFi setup (setup uses esp_wifi_init/start)
  esp_wifi_stop();
  delay(100);
  esp_wifi_set_mode(WIFI_MODE_AP);
  delay(100);
  esp_wifi_start();
  delay(200);

  // Configure softAP with SSID, password, and channel
  wifi_config_t apConfig = {};
  memset(&apConfig, 0, sizeof(apConfig));
  strcpy((char*)apConfig.ap.ssid, ssid);
  strcpy((char*)apConfig.ap.password, pass);
  apConfig.ap.ssid_len = strlen(ssid);
  apConfig.ap.max_connection = 4;
  apConfig.ap.authmode = strlen(pass) >= 8 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
  apConfig.ap.channel = 6;
  esp_wifi_set_config(WIFI_IF_AP, &apConfig);

  // Configure IP/AP settings and start DHCP
  WiFi.softAPConfig(FY_WS_IP, FY_WS_IP, FY_WS_SUBNET);

  mb_wifiStatus = "connecting...";
  snprintf(mb_webLog, sizeof(mb_webLog), "AP: %s", ssid);
  mb_showWebLog = true;
  mb_webLogMs = millis();
  Serial.printf("[webserver] SSID=%s PASS=%s IP=%s\n", ssid, pass, FY_WS_IP.toString().c_str());

  Serial.printf("[webserver] AP started: %s / %s @ %s\n", gApSSID, gApPass, gApIP.toString().c_str());

  // List all available files ordered by date
  gWebServer.on("/files", []() {
    snprintf(mb_webLog, sizeof(mb_webLog), "GET /files from %s", gWebServer.client().remoteIP().toString().c_str());
    mb_webLogMs = millis();
    String html = "<html><body><h1>flock-you files</h1><ul>";
    bool found = false;
    // List files from SD card if present
#if defined(USE_M5BASIC) && defined(USE_SDCARD)
    if (gStorageReady || gSdRawReady) {
      File root = SD.open("/");
      if (root) {
        String names[32];
        int count = 0;
        File f = root.openNextFile();
        while (f && count < 32) {
          if (!f.isDirectory()) {
            String name = f.name();
            if (name.startsWith("flock_you-") || name.startsWith("waypoints-")) {
              names[count++] = name;
            }
          }
          f = root.openNextFile();
        }
        root.close();
        for (int i = 0; i < count; i++) {
          for (int j = i + 1; j < count; j++) {
            if (names[j] < names[i]) {
              String tmp = names[i];
              names[i] = names[j];
              names[j] = tmp;
            }
          }
        }
        for (int i = 0; i < count; i++) {
          html += "<li><a href='/file?name=" + names[i] + "'>" + names[i] + "</a></li>";
          found = true;
        }
      }
    }
#endif
    // List files from SPIFFS if available
    if (fySpiffsReady) {
      fs::File root = SPIFFS.open("/");
      if (root) {
        String names[32];
        int count = 0;
        fs::File f = root.openNextFile();
        while (f && count < 32) {
          String name = f.name();
          if (name.startsWith("flock_you-") || name.startsWith("waypoints-")) {
            names[count++] = name;
          }
          f = root.openNextFile();
        }
        root.close();
        for (int i = 0; i < count; i++) {
          for (int j = i + 1; j < count; j++) {
            if (names[j] < names[i]) {
              String tmp = names[i];
              names[i] = names[j];
              names[j] = tmp;
            }
          }
        }
        for (int i = 0; i < count; i++) {
          html += "<li><a href='/file?name=" + names[i] + "'>" + names[i] + "</a></li>";
          found = true;
        }
      }
    }
    if (!found) html += "<li>(none yet)</li>";
    html += "</ul></body></html>";
    gWebServer.send(200, "text/html", html);
  });

  // Serve specific file by name — check both SD and SPIFFS
  gWebServer.on("/file", []() {
    String name = gWebServer.arg("name");
    snprintf(mb_webLog, sizeof(mb_webLog), "GET /file?name=%s from %s", name.c_str(), gWebServer.client().remoteIP().toString().c_str());
    mb_webLogMs = millis();

    // Try SD card first
#if defined(USE_M5BASIC) && defined(USE_SDCARD)
    {
      String sdPath = name;
      if (!sdPath.startsWith("/")) sdPath = String("/") + sdPath;
      File f = SD.open(sdPath.c_str(), "r");
      if (f) {
        String body = f.readString();
        f.close();
        gWebServer.send(200, "application/json", body);
        return;
      }
    }
#endif
    // Fall back to SPIFFS
    if (fySpiffsReady) {
      File f = SPIFFS.open(name.c_str(), "r");
      if (f) {
        String body = f.readString();
        f.close();
        gWebServer.send(200, "application/json", body);
        return;
      }
    }
    gWebServer.send(404, "application/json", "{\"error\":\"not found\"}");
  });

  // Root status endpoint
  gWebServer.on("/", []() {
    int detCount = fyDetCount;
    String html = "<html><body><h1>flock-you</h1>";
    html += "<p>Detections: " + String(detCount) + "</p>";
    html += "<p><a href='/files'>Browse files</a></p>";
    html += "</body></html>";
    gWebServer.send(200, "text/html", html);
  });

  gWebServer.begin();
  gWebServerActive = true;
  mb_wifiStatus = "connected";
}

void fyWebServerStop() {
  if (!gWebServerActive) return;
  gWebServer.stop();
  WiFi.softAPdisconnect(true);

  // Stop WiFi and restart in null mode for promiscuous scanning
  esp_wifi_stop();
  delay(100);
  esp_wifi_set_mode(WIFI_MODE_NULL);
  esp_wifi_start();
  delay(100);

  // Restore promiscuous mode + channel
  applyInitialChannel();
  esp_wifi_set_promiscuous(true);

  gWebServerActive = false;
  mb_wifiStatus = "disconnected";
  Serial.println("[webserver] stopped, resuming scanning");
}

void fyWebServerTick() {
  if (gWebServerActive) {
    gWebServer.handleClient();
  }
}

bool fyWebServerActive() {
  return gWebServerActive;
}
