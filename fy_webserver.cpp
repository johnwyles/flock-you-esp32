// flock-you-esp32 — Simple web server for detection data
// Serves JSON detections + status when toggled from promiscuous mode

#include "fy_webserver.h"
#include "fy_webserver_config.h"

extern bool mb_showWebLog;
extern char mb_webLog[120];
extern unsigned long mb_webLogMs;
extern const char *mb_wifiStatus;
#include "storage_backend.h"

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
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(FY_WS_IP, FY_WS_IP, FY_WS_SUBNET);
  bool ok = WiFi.softAP(ssid, pass, 6);
  mb_wifiStatus = "connecting...";
  snprintf(mb_webLog, sizeof(mb_webLog), "AP: %s", ssid);
  mb_showWebLog = true;
  mb_webLogMs = millis();
  Serial.printf("[webserver] SSID=%s PASS=%s IP=%s\n", ssid, pass, FY_WS_IP.toString().c_str());
  mb_wifiStatus = "connected";
  if (!ok) {
    Serial.println("[webserver] AP start failed");
    return;
  }
  Serial.printf("[webserver] AP started: %s / %s @ %s\n", gApSSID, gApPass, gApIP.toString().c_str());

  // List all available files ordered by date
  gWebServer.on("/files", []() {
    snprintf(mb_webLog, sizeof(mb_webLog), "GET /files from %s", gWebServer.client().remoteIP().toString().c_str());
    mb_webLogMs = millis();
    String html = "<html><body><h1>flock-you files</h1><ul>";
    bool found = false;
#if defined(USE_M5BASIC) && defined(USE_SDCARD)
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
#endif
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

  // Serve specific file by name
  gWebServer.on("/file", []() {
    String name = gWebServer.arg("name");
    snprintf(mb_webLog, sizeof(mb_webLog), "GET /file?name=%s from %s", name.c_str(), gWebServer.client().remoteIP().toString().c_str());
    mb_webLogMs = millis();
    File f = fyOpen(name.c_str(), "r");
    if (!f) {
      gWebServer.send(404, "application/json", "{\"error\":\"not found\"}");
      return;
    }
    String body = f.readString();
    f.close();
    String ct = (name.startsWith("waypoints-")) ? "application/json" : "application/json";
    gWebServer.send(200, ct, body);
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
}

void fyWebServerStop() {
  if (!gWebServerActive) return;
  gWebServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);
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
