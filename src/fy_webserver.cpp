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

  // Set WiFi to station mode explicitly (not promiscuous/scanner mode)
  WiFi.mode(WIFI_MODE_STA);
  delay(50);
  // Start the WiFi driver (WiFi.mode doesn't start it, begin does, but
  // after esp_wifi_stop() we need to ensure the driver is fresh)
  esp_wifi_start();
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
    // List files from SD card if present
    if (gStorageReady) {
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
          yield();  // prevent WiFi watchdog timeout
        }
        root.close();
        // Bubble sort by name
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
          // Strip leading "/" for URL (SD files have it, SPIFFS doesn't)
          String urlName = names[i];
          if (urlName.startsWith("/")) urlName = urlName.substring(1);
          html += "<div class='file-item'><span class='fname'>" + names[i] + "</span><div>";
          html += "<a class='btn' href='/file?name=" + urlName + "'>JSON</a>";
          html += "<a class='btn' href='/table?name=" + urlName + "'>Table</a></div></div>";
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
        // Bubble sort by name
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
          html += "<div class='file-item'><span class='fname'>" + names[i] + "</span><div>";
          html += "<a class='btn' href='/file?name=" + names[i] + "'>JSON</a>";
          html += "<a class='btn' href='/table?name=" + names[i] + "'>Table</a></div></div>";
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
        gWebServer.sendHeader("Content-Type", "application/json");
        gWebServer.streamFile(f, "application/json");
        f.close();
        return;
      }
    }
    // Fall back to SPIFFS
    if (fySpiffsReady) {
      File f = SPIFFS.open(name.c_str(), "r");
      if (f) {
        gWebServer.sendHeader("Content-Type", "application/json");
        gWebServer.streamFile(f, "application/json");
        f.close();
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
        gWebServer.send(200, "application/json", body);
        return;
      }
    }
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

  // Table view — parse JSON on server and render as HTML table
  gWebServer.on("/table", []() {
    String name = gWebServer.arg("name");
    snprintf(mb_webLog, sizeof(mb_webLog), "GET /table?name=%s from %s", name.c_str(), gWebServer.client().remoteIP().toString().c_str());
    mb_webLogMs = millis();

    String html = "<html><head><title>Table: " + name + "</title>" + String(pageCSS) + "</head><body><div class='container'>";
    html += "<h1>Detections: " + name + "</h1>";
    html += "<p><a class='btn btn-back' href='/files'>← Back to files</a></p>";

    // Read the file
    String body = "";
    {
      String sdPath = name;
      if (!sdPath.startsWith("/")) sdPath = String("/") + sdPath;
      File f = SD.open(sdPath.c_str(), "r");
      if (f) {
        body = f.readString();
        f.close();
      }
    }
    if (body.length() == 0 && fySpiffsReady) {
      File f = SPIFFS.open(name.c_str(), "r");
      if (f) {
        body = f.readString();
        f.close();
      }
    }

    if (body.length() == 0) {
      html += "<div class='card'><p>File not found.</p></div>";
    } else {
      // Parse JSON lines — first line may be metadata header {"v":1,"count":N,...}
      // Skip it, then parse each subsequent line as a detection JSON object.
      // Collect all unique column names, then generate the table.

      // First pass: collect column names from all data lines
      String cols = "";
      int start = 0;
      int lineNum = 0;
      while (start < body.length()) {
        int nl = body.indexOf('\n', start);
        if (nl == -1) nl = body.length();
        String line = body.substring(start, nl);
        line.trim();
        start = nl + 1;
        if (line.length() < 10) continue;
        // Skip metadata header line
        if (lineNum == 0 && line.charAt(0) == '{' && line.indexOf("\"v\":") != -1) {
          lineNum++;
          continue;
        }
        lineNum++;
        // Extract top-level keys (strings before colon, not inside nested objects)
        int depth = 0;
        int pos = 0;
        while (pos < line.length()) {
          char c = line.charAt(pos);
          if (c == '{') depth++;
          else if (c == '}') depth--;
          if (depth == 1 && c == '"') {
            int q2 = line.indexOf('"', pos + 1);
            if (q2 != -1) {
              String key = line.substring(pos + 1, q2);
              // Only top-level keys (not keys inside nested objects)
              if (cols.indexOf("|" + key + "|") == -1) {
                cols += key + "|";
              }
              pos = q2 + 1;
              // Skip to the colon
              int colonPos = line.indexOf(':', pos);
              if (colonPos != -1) pos = colonPos + 1;
              continue;
            }
          }
          pos++;
        }
      }

      // Generate table header
      html += "<table><tr>";
      int ci = 0;
      while (ci < cols.length()) {
        int pipe = cols.indexOf('|', ci);
        if (pipe == -1) break;
        String col = cols.substring(ci, pipe);
        if (col.length() > 0) {
          html += "<th>" + col + "</th>";
        }
        ci = pipe + 1;
      }
      html += "</tr>";

      // Generate data rows
      start = 0;
      lineNum = 0;
      int dataRows = 0;
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
        dataRows++;

        html += "<tr>";
        // For each column, extract the value from this JSON line
        ci = 0;
        while (ci < cols.length()) {
          int pipe = cols.indexOf('|', ci);
          if (pipe == -1) break;
          String col = cols.substring(ci, pipe);
          ci = pipe + 1;
          if (col.length() == 0) continue;

          String val = "";
          // Find "col": in the JSON line
          String searchKey = "\"" + col + "\"";
          int pos = line.indexOf(searchKey);
          if (pos != -1) {
            int colonPos = line.indexOf(':', pos + searchKey.length());
            if (colonPos != -1) {
              pos = colonPos + 1;
              while (pos < line.length() && (line.charAt(pos) == ' ' || line.charAt(pos) == '\t')) pos++;
              if (pos < line.length() && line.charAt(pos) == '"') {
                // String value
                int endQ = line.indexOf('"', pos + 1);
                if (endQ != -1) {
                  val = line.substring(pos + 1, endQ);
                  val.replace("\\\"", "\"");
                  val.replace("\\\\", "\\");
                  val.replace("\\n", "\n");
                }
              } else {
                // Numeric/boolean value or nested object/array
                int endVal = line.indexOf(',', pos);
                int endBrace = line.indexOf('}', pos);
                int endBracket = line.indexOf(']', pos);
                // Find the earliest terminator
                int endVal2 = line.length();
                if (endVal != -1) endVal2 = endVal;
                if (endBrace != -1 && endBrace < endVal2) endVal2 = endBrace;
                if (endBracket != -1 && endBracket < endVal2) endVal2 = endBracket;
                val = line.substring(pos, endVal2);
                val.trim();
              }
            }
          }
          // HTML-escape
          val.replace("&", "&amp;");
          val.replace("<", "&lt;");
          val.replace(">", "&gt;");
          html += "<td>" + val + "</td>";
        }
        html += "</tr>";
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
