// flock-you-esp32 — Simple web server for detection data
// Serves JSON detections + status when toggled from promiscuous mode

#include "fy_webserver.h"
#include "fy_webserver_config.h"
#include <WiFi.h>
#include "esp_wifi.h"
#include "esp_netif.h"
#include "fy_globals.h"
#include "storage_backend.h"
#if defined(ENABLE_BLE_SCAN) && ENABLE_BLE_SCAN
extern void bleScanStop();
extern void bleScanStartCoex();
#endif

extern bool mb_showWebLog;
extern char mb_webLog[120];
extern unsigned long mb_webLogMs;
extern const char *mb_wifiStatus;

extern bool fySpiffsReady;
extern int fyDetCount;
extern bool gStorageReady;

static bool gWebServerActive = false;
static IPAddress gApIP(192, 168, 4, 1);
static const char *gApSSID = "flock-you";
static const char *gApPass = "flockyou";

// File list cache — populated at web server start, used by /files endpoint
// This avoids slow SD enumeration during HTTP request handling.
static String gFileCache = "";
static unsigned long gFileCacheMs = 0;

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
  // CRITICAL: Stop BLE coex scan FIRST — the BLE scanner uses the WiFi
  // driver and calling esp_wifi_stop() while BLE is active causes a crash.
#if defined(ENABLE_BLE_SCAN) && ENABLE_BLE_SCAN
  bleScanStop();
  Serial.println("[webserver] BLE scan stopped");
  delay(100);
#endif
  esp_wifi_set_promiscuous(false);
  delay(100);

  // Stop WiFi driver completely
  esp_wifi_stop();
  delay(100);

  // Start the WiFi driver in station mode using ESP-IDF API
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_start();
  delay(100);

  // Set WiFi configuration using ESP-IDF (ssid/pass already in fyWsReadConfig)
  wifi_config_t sta_cfg = {};
  strncpy((char*)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid) - 1);
  strncpy((char*)sta_cfg.sta.password, pass, sizeof(sta_cfg.sta.password) - 1);
  sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
  delay(100);

  // Connect using ESP-IDF — this triggers the connection + DHCP
  Serial.printf("[webserver] Connecting to %s...\n", ssid);
  esp_wifi_connect();
  unsigned long startMs = millis();
  bool connected = false;

  // Wait for connection + DHCP lease (up to 35 seconds)
  wifi_ap_record_t ap_info;
  while (millis() - startMs < 35000) {
    delay(500);
    Serial.printf("[webserver] connecting... %lus\n", (millis() - startMs) / 1000);
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
      // WiFi connected at the 802.11 layer — wait for DHCP/TCPIP
      tcpip_adapter_ip_info_t ip_info;
      if (tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info) == 0) {
        if (ip_info.ip.addr != 0 && ip_info.ip.addr != IPADDR_BROADCAST) {
          connected = true;
          break;
        }
      }
    }
  }

  if (!connected) {
    Serial.println("[webserver] WiFi connect failed, aborting");
    mb_wifiStatus = "connect failed";
    // Clean up WiFi before restoring scanning
    esp_wifi_stop();
    delay(100);
    esp_wifi_set_mode(WIFI_MODE_NULL);
    esp_wifi_start();
    delay(100);
    // Restore promiscuous mode + channel
    applyInitialChannel();
    esp_wifi_set_promiscuous(true);
    return;
  }

  // WiFi connected — get DHCP-assigned IP using ESP-IDF API
  tcpip_adapter_ip_info_t ip_info;
  tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info);
  char ip_str[24];
  snprintf(ip_str, sizeof(ip_str), "%s", ip4addr_ntoa((const ip4_addr_t*)&ip_info.ip));
  Serial.printf("[webserver] Connected! IP: %s\n", ip_str);

  mb_wifiStatus = "connected";
  snprintf(mb_webLog, sizeof(mb_webLog), "SSID: %s\r\nPASS: %s\r\nIP: %s", ssid, pass, ip_str);

  // Populate public AP info for display
  strncpy(gWebServerSSID, ssid, sizeof(gWebServerSSID) - 1);
  gWebServerSSID[sizeof(gWebServerSSID) - 1] = '\0';
  strncpy(gWebServerPass, pass, sizeof(gWebServerPass) - 1);
  gWebServerPass[sizeof(gWebServerPass) - 1] = '\0';
  snprintf(gWebServerIP, sizeof(gWebServerIP), "%s", ip_str);
  mb_showWebLog = true;
  mb_webLogMs = millis();
  Serial.printf("[webserver] Connected to %s, web server on %s:80\n", ssid, ip_str);

  // File list cache is populated below after WiFi connects.
  gFileCache = "";
  gFileCacheMs = millis();

  // CSS styles shared across all pages
  static const char *pageCSS =
    "<style>"
    "*{box-sizing:border-box}"
    "body{font-family:'Courier New',monospace;background:linear-gradient(135deg,#0f0f23,#1a1a2e);color:#e0e0e0;margin:0;padding:20px;}"
    "h1{color:#00d4ff;border-bottom:2px solid #00d4ff;padding-bottom:8px;text-shadow:0 0 8px rgba(0,212,255,0.5);}"
    ".container{max-width:900px;margin:0 auto}"
    ".card{background:#16213e;border-radius:8px;padding:20px;margin-bottom:15px;box-shadow:0 2px 8px rgba(0,0,0,0.3)}"
    ".file-item{display:flex;align-items:center;justify-content:space-between;background:#16213e;border-radius:6px;padding:10px 15px;margin-bottom:5px}"
    ".file-item .fname{color:#00d4ff;font-weight:bold}"
    ".btn{display:inline-block;padding:4px 14px;background:#0f3460;color:#00d4ff;border:1px solid #00d4ff;border-radius:4px;transition:all 0.2s}"
    ".btn:hover{background:#00d4ff;color:#0f3460;text-shadow:0 0 4px rgba(255,255,255,0.8)}"
    ".btn-back{background:#222;color:#888}"
    ".btn-back:hover{background:#444;color:#aaa}"
    "a{color:#00d4ff;text-decoration:none}"
    "a:hover{color:#00ff88;text-decoration:underline}"
    "table{border-collapse:collapse;margin:15px 0;width:100%;overflow:hidden;border-radius:6px}"
    "th,td{border:1px solid #333;padding:4px 8px;text-align:left;font-size:13px}"
    "th{background:linear-gradient(135deg,#16213e,#0f3460);color:#00d4ff;font-weight:bold}"
    "tr:nth-child(even){background:#16213e}"
    "tr:nth-child(odd){background:#1a1a2e}"
    "tr:hover{background:#0f3460}"
    ".status-bar{color:#888;font-size:12px;margin-top:10px}"
    "</style>";

  // List all available files ordered by date
  gWebServer.on("/files", []() {
    snprintf(mb_webLog, sizeof(mb_webLog), "GET /files from %s", gWebServer.client().remoteIP().toString().c_str());
    mb_webLogMs = millis();
    String html = "<html><head>" + String(pageCSS) + "</head><body><div class='container'><h1>flock-you files</h1><div class='card'>";
    bool found = false;

    // Use cached file list (populated at web server start)
    // This avoids slow SD card enumeration during HTTP request handling.
    if (gFileCache.length() > 0) {
      int idx = 0;
      while (idx < gFileCache.length()) {
        int pipe = gFileCache.indexOf('|', idx);
        if (pipe == -1) break;
        String fname = gFileCache.substring(idx, pipe);
        idx = pipe + 1;
        if (fname.length() == 0) continue;
        html += "<div class='file-item'><span class='fname'>" + fname + "</span><div>";
        html += "<a class='btn' href='/file?name=" + fname + "'>JSON</a>";
        html += "<a class='btn' href='/table?name=" + fname + "'>Table</a></div></div>";
        found = true;
      }
    }
    // Fallback: enumerate SD card if cache is empty (shouldn't normally happen)
    if (!found && gStorageReady) {
      yield();
      File root = SD.open("/");
      if (root) {
        yield();
        String names[32];
        int count = 0;
        File f = root.openNextFile();
        while (f && count < 32) {
          if (!f.isDirectory()) {
            String fname = f.name();
            if (fname.startsWith("flock_you-") || fname.startsWith("waypoints-")) {
              names[count++] = fname;
            }
          }
          f = root.openNextFile();
          yield();
        }
        root.close();
        for (int i = 0; i < count; i++) { for (int j = i + 1; j < count; j++) { if (names[j] < names[i]) { String tmp = names[i]; names[i] = names[j]; names[j] = tmp; } } }
        for (int i = 0; i < count; i++) {
          String urlName = names[i];
          if (urlName.startsWith("/")) urlName = urlName.substring(1);
          html += "<div class='file-item'><span class='fname'>" + names[i] + "</span><div>";
          html += "<a class='btn' href='/file?name=" + urlName + "'>JSON</a>";
          html += "<a class='btn' href='/table?name=" + urlName + "'>Table</a></div></div>";
          found = true;
        }
      }
    }
    if (!found) html += "<p>(none yet)</p>";
    html += "</div></div></body></html>";
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
        yield();
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
        yield();
        gWebServer.send(200, "application/json", body);
        return;
      }
    }
    gWebServer.send(404, "application/json", "{\"error\":\"not found\"}");
  });

  // Serve raw JSON file — same as /file, kept for compatibility
  gWebServer.on("/json", []() {
    String name = gWebServer.arg("name");
    // Try SD card first
    {
      String sdPath = name;
      if (!sdPath.startsWith("/")) sdPath = String("/") + sdPath;
      File f = SD.open(sdPath.c_str(), "r");
      if (f) {
        String body = f.readString();
        f.close();
        yield();
        gWebServer.send(200, "application/json", body);
        return;
      }
    }
    if (fySpiffsReady) {
      File f = SPIFFS.open(name.c_str(), "r");
      if (f) {
        String body = f.readString();
        f.close();
        yield();
        gWebServer.send(200, "application/json", body);
        return;
      }
    }
    gWebServer.send(404, "application/json", "{\"error\":\"not found\"}");
  });

  // Table view — parse JSON on server and render as HTML table
  gWebServer.on("/table", []() {
    String name = gWebServer.arg("name");
    snprintf(mb_webLog, sizeof(mb_webLog), "GET /table?name=%s from %s", name.c_str(), gWebServer.client().remoteIP().toString().c_str());
    mb_webLogMs = millis();

    String html = "<html><head><title>Table: " + name + "</title>" + String(pageCSS) + "</head><body><div class='container'>";
    html += "<h1>Detections: " + name + "</h1>";
    html += "<p><a class='btn btn-back' href='/files'>Back to files</a></p>";

    // Read the file
    String body = "";
    {
      String sdPath = name;
      if (!sdPath.startsWith("/")) sdPath = String("/") + sdPath;
      File f = SD.open(sdPath.c_str(), "r");
      if (f) {
        body = f.readString();
        f.close();
        yield();
      }
    }
    if (body.length() == 0 && fySpiffsReady) {
      File f = SPIFFS.open(name.c_str(), "r");
      if (f) {
        body = f.readString();
        f.close();
        yield();
      }
    }

    if (body.length() == 0) {
      html += "<div class='card'><p>File not found.</p></div>";
    } else {
      // Simple approach: each line is a JSON object (skip metadata header)
      // Parse in a single pass to minimize memory usage.
      int start = 0;
      int lineNum = 0;
      int dataRows = 0;
      String firstLine = "";

      // First pass: find columns from the first data line
      bool colsFound = false;
      while (start < body.length()) {
        int nl = body.indexOf('\n', start);
        if (nl == -1) nl = body.length();
        String line = body.substring(start, nl);
        line.trim();
        start = nl + 1;
        if (line.length() < 10) continue;
        // Skip metadata header
        if (lineNum == 0 && line.charAt(0) == '{' && line.indexOf("\"v\":") != -1) {
          lineNum++;
          continue;
        }
        lineNum++;
        if (!colsFound) {
          firstLine = line;
          colsFound = true;
        }
        dataRows++;
        yield();
      }

      // Extract column names from first data line
      html += "<table>";
      // Header row with column names
      html += "<tr>";
      int p = 0;
      int depth = 0;
      while (p < firstLine.length()) {
        char c = firstLine.charAt(p);
        if (c == '{') depth++;
        else if (c == '}') depth--;
        if (depth == 1 && c == '"') {
          int q2 = firstLine.indexOf('"', p + 1);
          if (q2 != -1) {
            String key = firstLine.substring(p + 1, q2);
            html += "<th>" + key + "</th>";
            p = q2 + 1;
            // Skip to colon
            int colon = firstLine.indexOf(':', p);
            if (colon != -1) p = colon + 1;
            continue;
          }
        }
        p++;
      }
      html += "</tr>";

      // Second pass: generate rows
      start = 0;
      lineNum = 0;
      while (start < body.length()) {
        int nl = body.indexOf('\n', start);
        if (nl == -1) nl = body.length();
        String line = body.substring(start, nl);
        line.trim();
        start = nl + 1;
        if (line.length() < 10) continue;
        if (lineNum == 0 && line.charAt(0) == '{' && line.indexOf("\"v\":") != -1) {
          lineNum++;
          continue;
        }
        lineNum++;

        html += "<tr>";
        // Re-extract columns from this line's first data line pattern
        p = 0;
        depth = 0;
        while (p < line.length()) {
          char c = line.charAt(p);
          if (c == '{') depth++;
          else if (c == '}') depth--;
          if (depth == 1 && c == '"') {
            int q2 = line.indexOf('"', p + 1);
            if (q2 != -1) {
              String key = line.substring(p + 1, q2);
              // Skip to colon
              int colon = line.indexOf(':', q2 + 1);
              if (colon != -1) {
                p = colon + 1;
                while (p < line.length() && (line.charAt(p) == ' ' || line.charAt(p) == '\t')) p++;
                String val = "";
                if (p < line.length() && line.charAt(p) == '"') {
                  int endQ = line.indexOf('"', p + 1);
                  if (endQ != -1) {
                    val = line.substring(p + 1, endQ);
                    val.replace("\\\"", "\"");
                    val.replace("\\\\", "\\");
                  }
                } else {
                  int endVal = line.indexOf(',', p);
                  if (endVal == -1) endVal = line.indexOf('}', p);
                  if (endVal == -1) endVal = line.indexOf(']', p);
                  if (endVal == -1) endVal = line.length();
                  val = line.substring(p, endVal);
                  val.trim();
                }
                val.replace("&", "&amp;");
                val.replace("<", "&lt;");
                val.replace(">", "&gt;");
                html += "<td>" + val + "</td>";
                p = q2 + 1;
                continue;
              }
            }
          }
          p++;
        }
        html += "</tr>";
        yield();  // prevent watchdog
      }
      html += "</table></div>";
      html += "<div class='status-bar'>" + String(dataRows) + " rows loaded</div>";
    }

    html += "</div></body></html>";
    gWebServer.send(200, "text/html", html);
  });

  // Root status endpoint
  gWebServer.on("/", []() {
    int detCount = fyDetCount;
    String html = "<html><head>" + String(pageCSS) + "</head><body><div class='container'>";
    html += "<h1>flock-you</h1>";
    html += "<div class='card'><p>Detections: " + String(detCount) + "</p>";
    html += "<p><a class='btn' href='/files'>Browse files</a></p></div>";
    html += "<div class='status-bar'>Web server running on <strong>" + String(gWebServerIP) + ":80</strong></div>";
    html += "</div></body></html>";
    gWebServer.send(200, "text/html", html);
  });

  // Populate file list cache before starting HTTP server.
  // SD enumeration is slow and blocks WiFi — do it once at startup
  // instead of during HTTP request handling.
  gFileCache = "";
  if (gStorageReady) {
    yield();
    File root = SD.open("/");
    if (root) {
      File f = root.openNextFile();
      while (f) {
        if (!f.isDirectory()) {
          String fname = f.name();
          if (fname.startsWith("flock_you-") || fname.startsWith("waypoints-")) {
            if (fname.startsWith("/")) fname = fname.substring(1);
            gFileCache += fname;
            gFileCache += "|";
          }
        }
        f = root.openNextFile();
        yield();
      }
      root.close();
    }
  }
  if (fySpiffsReady) {
    fs::File root = SPIFFS.open("/");
    if (root) {
      fs::File f = root.openNextFile();
      while (f) {
        String fname = f.name();
        if (fname.startsWith("flock_you-") || fname.startsWith("waypoints-")) {
          gFileCache += fname;
          gFileCache += "|";
        }
        f = root.openNextFile();
        yield();
      }
      root.close();
    }
  }
  gFileCacheMs = millis();

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

  // Restart BLE coex scan (was stopped in fyWebServerStart)
#if defined(ENABLE_BLE_SCAN) && ENABLE_BLE_SCAN
  bleScanStartCoex();
  delay(100);
#endif

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
