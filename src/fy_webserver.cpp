// flock-you-esp32 — Simple web server for detection data
// Serves JSON detections + status when toggled from promiscuous mode

#include "fy_webserver.h"
#include "fy_webserver_config.h"
#include <WiFi.h>
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
extern bool gStorageReady;

static bool gWebServerActive = false;
static IPAddress gApIP(192, 168, 4, 1);
static const char *gApSSID = "flock-you";
static const char *gApPass = "flockyou";

// Public web server AP info (read by main.cpp for display)
char gWebServerSSID[64] = "";
char gWebServerPass[64] = "";
char gWebServerIP[24] = "";

WebServer gWebServer(80);

void fyWebServerStart() {
  if (gWebServerActive) return;
  char ssid[64] = {0};
  char pass[64] = {0};
  fyWsReadConfig(ssid, sizeof(ssid), pass, sizeof(pass));

  // Switch from promiscuous AP-scanning mode to station mode to connect
  // to an existing WiFi network (credentials from .env / build_flags).
  esp_wifi_set_promiscuous(false);
  delay(100);

  // Stop WiFi driver completely
  esp_wifi_stop();
  delay(100);

  // Connect as a station to the target WiFi network
  WiFi.begin(ssid, pass);
  delay(100);

  // Wait for connection + DHCP lease (up to 30 seconds)
  Serial.printf("[webserver] Connecting to %s...\n", ssid);
  unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < 30000) {
    delay(500);
    Serial.printf("[webserver] connecting... %lus\n", (millis() - startMs) / 1000);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[webserver] WiFi connect failed, aborting");
    mb_wifiStatus = "connect failed";
    // Restore promiscuous scanning
    esp_wifi_set_promiscuous(true);
    return;
  }

  // WiFi connected — get DHCP-assigned IP
  IPAddress ip = WiFi.localIP();
  Serial.printf("[webserver] Connected! IP: %s\n", ip.toString().c_str());

  mb_wifiStatus = "connected";
  snprintf(mb_webLog, sizeof(mb_webLog), "SSID: %s\r\nPASS: %s\r\nIP: %s", ssid, pass, ip.toString().c_str());

  // Populate public AP info for display
  strncpy(gWebServerSSID, ssid, sizeof(gWebServerSSID) - 1);
  gWebServerSSID[sizeof(gWebServerSSID) - 1] = '\0';
  strncpy(gWebServerPass, pass, sizeof(gWebServerPass) - 1);
  gWebServerPass[sizeof(gWebServerPass) - 1] = '\0';
  snprintf(gWebServerIP, sizeof(gWebServerIP), "%s", ip.toString().c_str());
  mb_showWebLog = true;
  mb_webLogMs = millis();
  Serial.printf("[webserver] Connected to %s, web server on %s:80\n", ssid, ip.toString().c_str());

  // List all available files ordered by date
  gWebServer.on("/files", []() {
    snprintf(mb_webLog, sizeof(mb_webLog), "GET /files from %s", gWebServer.client().remoteIP().toString().c_str());
    mb_webLogMs = millis();
    String html = "<html><body><h1>flock-you files</h1><ul>";
    bool found = false;
    // List files from SD card if present
    if (gStorageReady) {
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
  delay(100);  // let TCP listener bind
  gWebServerActive = true;
  mb_wifiStatus = "connected";
  Serial.println("[webserver] HTTP server on port 80 ready");
}

void fyWebServerStop() {
  if (!gWebServerActive) return;
  gWebServer.stop();

  // Stop WiFi and restart in null mode for promiscuous scanning.
  // Use the same ESP-IDF API sequence that setup() used originally.
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
  mb_showWebLog = false;
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
