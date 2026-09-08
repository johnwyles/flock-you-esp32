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

  // Serve detection data as JSON
  gWebServer.on("/detections", []() {
    snprintf(mb_webLog, sizeof(mb_webLog), "GET /detections from %s", gWebServer.client().remoteIP().toString().c_str());
    mb_webLogMs = millis();
    Serial.printf("[webserver] %s\n", mb_webLog);
    File f = fyOpen("/flock_you-session.json", "r");
    if (!f) {
      gWebServer.send(404, "application/json", "{\"error\":\"no data\"}");
      return;
    }
    String body = f.readString();
    f.close();
    gWebServer.send(200, "application/json", body);
  });

  // Status endpoint
  gWebServer.on("/", []() {
    int detCount = fyDetCount;
    String html = "<html><body><h1>flock-you</h1>";
    html += "<p>Detections: " + String(detCount) + "</p>";
    html += "<p><a href='/detections'>/detections</a></p>";
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
