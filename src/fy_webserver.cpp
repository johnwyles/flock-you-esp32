// flock-you-esp32 — Simple web server for detection data
// Serves JSON detections + status when toggled from promiscuous mode

#include "fy_webserver.h"
#include "fy_webserver_config.h"
#include <WiFi.h>
#include "esp_wifi.h"
#include "fy_globals.h"
#include "storage_backend.h"
#include "fy_module_diag.h"
#include "fy_json_lite.h"
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

// (Re)build the file list: "fname|source|..." where source is "SD" or
// "SPIF". Called at web start AND when /files is opened (throttled to once
// per 3 s), so files created while the web server is up (new track files,
// saves) show up without restarting the server.
static void fyRefreshFileCache() {
  // Format: "fname|source|fname|source|..." where source is "SD" or "SPIF"
  gFileCache = "";
  if (gStorageReady) {
    yield();
    File root = SD.open("/");
    if (root) {
      File f = root.openNextFile();
      while (f) {
        if (!f.isDirectory()) {
          String fname = f.name();
          if (fname.startsWith("flock_you-") || fname.startsWith("waypoints-") || fname.startsWith("track-") || fname.startsWith("/track-") || fname.startsWith("/waypoints-")) {
            if (fname.startsWith("/")) fname = fname.substring(1);
            gFileCache += fname;
            gFileCache += "|SD|";
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
        if (fname.startsWith("flock_you-") || fname.startsWith("waypoints-") || fname.startsWith("track-") || fname.startsWith("/track-") || fname.startsWith("/waypoints-")) {
          gFileCache += fname;
          gFileCache += "|SPIF|";
        }
        f = root.openNextFile();
        yield();
      }
      root.close();
    }
  }
  gFileCacheMs = millis();
}

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

  // Use Arduino WiFi library for clean mode transition.
  // WiFi.begin() handles: mode switch, WiFi start, connection, DHCP.
  // We must NOT call esp_wifi_stop() — the Arduino WiFi library's
  // internal state won't recover properly, causing "dhcp client start failed".
  WiFi.mode(WIFI_MODE_STA);
  delay(100);

  // Connect as a station to the target WiFi network
  Serial.printf("[webserver] Connecting to %s...\n", ssid);
  WiFi.begin(ssid, pass);
  unsigned long startMs = millis();
  bool connected = false;

  // Wait for connection + DHCP lease (up to 30 seconds)
  while (millis() - startMs < 30000) {
    delay(500);
    Serial.printf("[webserver] connecting... %lus\n", (millis() - startMs) / 1000);
    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
      break;
    }
  }

  if (!connected) {
    Serial.println("[webserver] WiFi connect failed, aborting");
    mb_wifiStatus = "connect failed";
    // Clean up: disconnect WiFi, switch back to NULL mode
    WiFi.disconnect(true);
    WiFi.mode(WIFI_MODE_NULL);
    delay(100);
    // Restore promiscuous mode + channel
    applyInitialChannel();
    esp_wifi_set_promiscuous(true);
    // Restart BLE coex scan (was stopped in fyWebServerStart)
#if defined(ENABLE_BLE_SCAN) && ENABLE_BLE_SCAN
    bleScanStartCoex();
    delay(100);
#endif
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
    "table{border-collapse:collapse;margin:15px 0;width:100%;overflow:hidden;border-radius:6px;table-layout:auto;max-height:70vh;overflow-y:auto}"
    "th,td{border:1px solid #333;padding:4px 8px;text-align:left;font-size:12px;min-width:100px;}"
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

      // Get the WiFi client directly for streaming
      WiFiClient client = gWebServer.client();

      // Send HTTP headers
      client.print(
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n"
      );

      // Stream HTML response in chunks
      client.print("<html><head>");
      client.print(pageCSS);
      client.print("</head><body><div class='container'><h1>flock-you files</h1><div class='card'>");
      bool found = false;

      if (millis() - gFileCacheMs > 3000) fyRefreshFileCache();
      // Use cached file list (refreshed above if older than 3 s)
      // Format: "fname|source|fname|source|..." where source is "SD" or "SPIF"
      if (gFileCache.length() > 0) {
        int idx = 0;
        int entryIdx = 0;
        int totalItems = 0;
        // Count total items first
        for (int i = 0; i < gFileCache.length(); i++) {
          if (gFileCache[i] == '|') totalItems++;
        }
        totalItems /= 2; // fname|source = 2 pipes per item
        // Bubble sort by name (simple approach for small lists)
        String names[64];
        String sources[64];
        int count = 0;
        idx = 0;
        while (idx < gFileCache.length() && count < 64) {
          int pipe1 = gFileCache.indexOf('|', idx);
          if (pipe1 == -1) break;
          int pipe2 = gFileCache.indexOf('|', pipe1 + 1);
          if (pipe2 == -1) break;
          names[count] = gFileCache.substring(idx, pipe1);
          sources[count] = gFileCache.substring(pipe1 + 1, pipe2);
          idx = pipe2 + 1;
          count++;
        }
        // Sort by name
        for (int i = 0; i < count; i++) {
          for (int j = i + 1; j < count; j++) {
            if (names[j] < names[i]) {
              String tn = names[i]; names[i] = names[j]; names[j] = tn;
              String ts = sources[i]; sources[i] = sources[j]; sources[j] = ts;
            }
          }
        }
        // Render sorted
        for (int i = 0; i < count; i++) {
          String srcLabel = sources[i];
          if (srcLabel == "SD") srcLabel = "SD Card";
          else if (srcLabel == "SPIF") srcLabel = "SPIFFS";
          client.print("<div class='file-item'><span class='fname'>");
          client.print(names[i]);
          client.print(" <span class='status-bar'>(");
          client.print(srcLabel);
          client.print(")</span></span><div>");
          client.print("<a class='btn' href='/file?name=");
          client.print(names[i]);
          client.print("'>JSON</a>");
          client.print("<a class='btn' href='/table?name=");
          client.print(names[i]);
          client.print("'>Table</a></div></div>");
          found = true;
          entryIdx++;
        }
      }
      // Fallback: enumerate SD card if cache is empty (shouldn't normally happen)
      if (!found && gStorageReady) {
        yield();
        File root = SD.open("/");
        if (root) {
          yield();
          String names[64];
          int count = 0;
          File f = root.openNextFile();
          while (f && count < 64) {
            if (!f.isDirectory()) {
              String fname = f.name();
              if (fname.startsWith("flock_you-") || fname.startsWith("waypoints-") || fname.startsWith("track-") || fname.startsWith("/track-") || fname.startsWith("/waypoints-")) {
                names[count++] = fname;
              }
            }
            f = root.openNextFile();
            yield();
          }
          root.close();
          for (int i = 0; i < count; i++) {
            for (int j = i + 1; j < count; j++) {
              if (names[j] < names[i]) {
                String tmp = names[i]; names[i] = names[j]; names[j] = tmp;
              }
            }
          }
          for (int i = 0; i < count; i++) {
            String urlName = names[i];
            if (urlName.startsWith("/")) urlName = urlName.substring(1);
            client.print("<div class='file-item'><span class='fname'>");
            client.print(names[i]);
            client.print("</span><div>");
            client.print("<a class='btn' href='/file?name=");
            client.print(urlName);
            client.print("'>JSON</a>");
            client.print("<a class='btn' href='/table?name=");
            client.print(urlName);
            client.print("'>Table</a></div></div>");
            found = true;
          }
        }
      }
      if (!found) client.print("<p>(none yet)</p>");
      client.print("</div></div></body></html>");
      client.stop();
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
  // Uses WiFiClient directly to stream response — avoids WebServer timeout
  // during slow String-based JSON parsing on marginal WiFi links.
  // File format: line 0 = metadata header, line 1+ = JSON objects (either
  // one per line, or a single JSON array on one line with [{...},{...}]).
  gWebServer.on("/table", []() {
    String name = gWebServer.arg("name");
    snprintf(mb_webLog, sizeof(mb_webLog), "GET /table?name=%s from %s", name.c_str(), gWebServer.client().remoteIP().toString().c_str());
    mb_webLogMs = millis();

    // Read the file (from SD or SPIFFS)
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

    // Get the WiFi client directly for streaming
    WiFiClient client = gWebServer.client();

    // Send HTTP headers
    client.print(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html\r\n"
      "Connection: close\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "\r\n"
    );
    // Stream HTML response in chunks to avoid timeout
    client.print("<html><head><title>Table: ");
    client.print(name);
    client.print("</title>");
    client.print(pageCSS);  // include shared CSS
    client.print("</head><body><div class='container'>");
    client.print("<h1>Detections: ");
    client.print(name);
    client.print("</h1>");
    client.print("<p><a class='btn btn-back' href='/files'>Back to files</a></p>");
    client.flush();

    if (body.length() == 0) {
      client.print("<div class='card'><p>File not found.</p></div>");
      client.print("</div></body></html>");
      client.stop();
      return;
    }

    // Parse with fy_json_lite.h. Detection files are a metadata line + one
    // JSON array; waypoint/track files are JSON Lines. Columns are the union
    // of all rows' keys in first-seen order (nested objects flattened to
    // "gps.lat" etc.), and each cell is looked up BY KEY, so a row that lacks
    // a field gets an empty cell instead of shifting the rest left/right.
    const char *buf = body.c_str();
    size_t blen = body.length();
    size_t start = 0;
    if (name.startsWith("flock_you-") || name.startsWith("/flock_you-")) {
      int arr = body.indexOf('[');
      if (arr >= 0) start = (size_t)arr;  // skip {"v":1,"count":...} header
    }
    static const int kMaxCols = 40;
    std::string cols[kMaxCols];
    int nCols = 0;
    size_t s0, e0, pos = start;
    int rowsTotal = 0;
    while (fyjson::nextObject(buf, blen, pos, s0, e0)) {
      auto addCol = [&](const std::string &k, const std::string &) {
        for (int i = 0; i < nCols; i++) if (cols[i] == k) return;
        if (nCols < kMaxCols) cols[nCols++] = k;
      };
      fyjson::members(buf, s0, e0, addCol);
      rowsTotal++;
      pos = e0;
      if ((rowsTotal & 15) == 0) yield();
    }
    if (rowsTotal == 0) {
      client.print("<div class='card'><p>No records in this file yet.</p></div>");
      client.print("</div></body></html>");
      client.stop();
      return;
    }
    auto esc = [](const std::string &in) {
      String o;
      for (char c : in) {
        if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '&') o += "&amp;";
        else o += c;
      }
      return o;
    };
    String row = "<table><thead><tr>";
    for (int i = 0; i < nCols; i++) row += "<th>" + esc(cols[i]) + "</th>";
    row += "</tr></thead><tbody>";
    client.print(row);

    int dataRows = 0;
    pos = start;
    std::string vals[kMaxCols];
    while (fyjson::nextObject(buf, blen, pos, s0, e0)) {
      for (int i = 0; i < nCols; i++) vals[i].clear();
      auto setVal = [&](const std::string &k, const std::string &v) {
        for (int i = 0; i < nCols; i++) if (cols[i] == k) { vals[i] = v; return; }
      };
      fyjson::members(buf, s0, e0, setVal);
      row = "<tr>";
      for (int i = 0; i < nCols; i++) row += "<td>" + esc(vals[i]) + "</td>";
      row += "</tr>";
      client.print(row);
      dataRows++;
      pos = e0;
      yield();
    }
    client.print("</tbody></table></div>");
    client.print("<div class='status-bar'>" + String(dataRows) + " rows loaded</div>");
    client.print("</div></body></html>");
    client.stop();
  });

  // Root status endpoint
  gWebServer.on("/", []() {
    snprintf(mb_webLog, sizeof(mb_webLog), "GET / from %s", gWebServer.client().remoteIP().toString().c_str());
    mb_webLogMs = millis();

    // Get the WiFi client directly for streaming
    WiFiClient client = gWebServer.client();

    // Send HTTP headers
    client.print(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html\r\n"
      "Connection: close\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "\r\n"
    );

    // Stream HTML response in chunks to avoid String allocation issues
    client.print("<html><head>");
    client.print(pageCSS);
    client.print("</head><body><div class='container'>");
    client.print("<h1>flock-you</h1>");
    client.print("<div class='card'><p>Detections: ");
    client.print(fyDetCount);
    client.print("</p>");
    client.print("<p><a class='btn' href='/files'>Browse files</a></p></div>");
    fyDiagPrintHtml(client);
    client.print("<div class='status-bar'>Web server running on <strong>");
    client.print(gWebServerIP);
    client.print(":80</strong></div>");
    client.print("</div></body></html>");
    client.stop();
  });

  // Optional-module diagnostics (GPS / CC1101) as JSON
  gWebServer.on("/modules", []() {
    static char json[3072];
    fyDiagJson(json, sizeof(json));
    gWebServer.sendHeader("Access-Control-Allow-Origin", "*");
    gWebServer.send(200, "application/json", json);
  });

  fyRefreshFileCache();

  gWebServer.begin();
  delay(100);  // let TCP listener bind
  gWebServerActive = true;
  mb_wifiStatus = "connected";
  Serial.println("[webserver] HTTP server on port 80 ready");
}

void fyWebServerStop() {
  if (!gWebServerActive) return;
  gWebServer.stop();
    fySaveSession();

  // Switch WiFi back to NULL mode for promiscuous scanning.
  WiFi.disconnect(true);
  WiFi.mode(WIFI_MODE_NULL);
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
