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
        size_t sz = f.size();
        String body = f.readString();
        f.close();
        // Set Content-Disposition so browser downloads it as a file
        gWebServer.sendContent("HTTP/1.1 200 OK\r\n");
        gWebServer.sendContent("Content-Type: application/json\r\n");
        gWebServer.sendContent(String("Content-Disposition: attachment; filename=\"" + name + "\"\r\n").c_str());
        gWebServer.sendContent(String("Content-Length: " + String(body.length()) + "\r\n").c_str());
        gWebServer.sendContent("Connection: close\r\n\r\n");
        gWebServer.sendContent(body.c_str());
        return;
      }
    }
    // Fall back to SPIFFS
    if (fySpiffsReady) {
      File f = SPIFFS.open(name.c_str(), "r");
      if (f) {
        String body = f.readString();
        f.close();
        gWebServer.sendContent("HTTP/1.1 200 OK\r\n");
        gWebServer.sendContent("Content-Type: application/json\r\n");
        gWebServer.sendContent(String("Content-Disposition: attachment; filename=\"" + name + "\"\r\n").c_str());
        gWebServer.sendContent(String("Content-Length: " + String(body.length()) + "\r\n").c_str());
        gWebServer.sendContent("Connection: close\r\n\r\n");
        gWebServer.sendContent(body.c_str());
        return;
      }
    }
    gWebServer.send(404, "application/json", "{\"error\":\"not found\"}");
  });

  // Table view — serves an HTML page with JavaScript that fetches the JSON
  // and renders it as a table with dynamic headers
  gWebServer.on("/table", []() {
    String name = gWebServer.arg("name");
    snprintf(mb_webLog, sizeof(mb_webLog), "GET /table?name=%s from %s", name.c_str(), gWebServer.client().remoteIP().toString().c_str());
    mb_webLogMs = millis();
    String html = "<html><head><title>Table: " + name + "</title>" + String(pageCSS) + "</head><body><div class='container'>";
    html += "<h1>Detections: " + name + "</h1>";
    html += "<p><a class='btn btn-back' href='/files'>← Back to files</a></p>";
    html += "<div class='card'><div id='table-container'></div></div>";
    html += "<div class='status-bar' id='status'>Loading...</div>";
    html += "<script>";
    // Fetch the JSON file (served by /file handler) and build a table
    html += "fetch('/file?name=" + name + "').then(r=>r.text()).then(raw=>{";
    html += "  // Skip the header line (JSON metadata), parse remaining lines as JSON";
    html += "  let lines = raw.trim().split('\\n');";
    html += "  let header = lines[0];";
    html += "  // If file has JSON header line starting with {, skip it";
    html += "  if(lines.length > 1 && lines[0].startsWith('{')) {";
    html += "    lines = lines.slice(1);";
    html += "  }";
    html += "  let data = lines.map(l => {try{return JSON.parse(l)}catch(e){return null}}).filter(x=>x)";
    html += "  if(data.length === 0) {";
    html += "    document.getElementById('table-container').innerHTML = '<p>No data found.</p>';";
    html += "    return;";
    html += "  }";
    // Collect all column names from all rows
    html += "  let cols = [];";
    html += "  data.forEach(row => {Object.keys(row).forEach(k => {if(cols.indexOf(k)===-1) cols.push(k)})})";
    html += "  let t = '<table><thead><tr>'";
    html += "  cols.forEach(c => {t += '<th>'+c+'</th>'})";
    html += "  t += '</tr></thead><tbody>'";
    html += "  data.forEach(row => {";
    html += "    t += '<tr>'";
    html += "    cols.forEach(c => {";
    html += "      let v = row[c];";
    html += "      if(v && typeof v === 'object') v = JSON.stringify(v)";
    html += "      else if(v === null) v = ''";
    html += "      else v = String(v)";
    html += "      t += '<td>'+v.replace(/</g,'&lt;')+'</td>'";
    html += "    })";
    html += "    t += '</tr>'";
    html += "  })";
    html += "  t += '</tbody></table>'";
    html += "  document.getElementById('table-container').innerHTML = t";
    html += "}).catch(e=>{";
    html += "  document.getElementById('table-container').innerHTML = '<p>Error loading data.</p>'";
    html += "})";
    html += "</script>";
    html += "</div></div></body></html>";
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
