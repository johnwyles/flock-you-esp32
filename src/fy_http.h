// flock-you-esp32 — minimal multi-connection HTTP server for the web UI
//
// Scope: accept and parse GET requests and hand them to route handlers. It
// replaces the Arduino WebServer library for this firmware and keeps the
// same call names the handlers already use (on, onNotFound, arg, uri,
// client, send, sendHeader, begin, stop, handleClient).
//
// Why: the library serves ONE connection at a time and waits up to 5 s on a
// connection that has not sent its request yet. Browsers open spare
// "preconnect" sockets that do exactly that, so real requests queued behind
// them; the browser then reset the queued sockets and the library spent
// ~9 s (10 x 1 s write retries) on each dead one before moving on
// ("[web] GET /files -> WRITE FAILED after 0 B ... in 8729 ms").
//
// This server keeps up to kSlots connections open at once, reads each one
// without blocking, dispatches whichever request is complete first, drops
// idle/dead sockets quickly, and writes with a short no-progress timeout
// that also watches for socket errors, so a dead client fails in ms.

#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <functional>

// Write all of buf to c. Fails fast on socket errors, and after timeoutMs
// with no progress. Returns bytes written (== len on success).
size_t fyHttpWriteAll(WiFiClient &c, const uint8_t *buf, size_t len, uint32_t timeoutMs = 3000);

class FyHttpServer {
 public:
  typedef std::function<void()> Handler;

  explicit FyHttpServer(uint16_t port) : _server(port, 8) {}

  void begin();
  void stop();
  void handleClient();

  void on(const char *path, Handler fn);
  void onNotFound(Handler fn) { _notFound = fn; }

  // Request accessors (valid inside a handler)
  String uri() const { return _uri; }
  String arg(const char *name) const;
  WiFiClient &client() { return _cur; }

  // Simple complete response; closes the connection.
  void send(int code, const char *contentType = "text/plain", const String &body = String());
  void sendHeader(const char *name, const String &value);

 private:
  static const int kSlots = 6;
  static const int kReqBuf = 768;
  struct Slot {
    WiFiClient c;
    bool used = false;
    unsigned long since = 0;
    uint16_t len = 0;
    char buf[kReqBuf];
  };
  struct Route {
    String path;
    Handler fn;
  };

  bool slotAlive(Slot &s);
  void dispatch(Slot &s);

  WiFiServer _server;
  Slot _slots[kSlots];
  Route _routes[16];
  int _nRoutes = 0;
  Handler _notFound;
  WiFiClient _cur;
  String _uri, _query, _extraHeaders;
  bool _running = false;
};
