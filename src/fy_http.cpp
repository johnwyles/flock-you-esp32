// flock-you-esp32 — minimal multi-connection HTTP server. See fy_http.h.

#include "fy_http.h"
#include <lwip/sockets.h>

static const uint32_t kIdleMs = 4000;  // close a socket that sent no full request

size_t fyHttpWriteAll(WiFiClient &c, const uint8_t *buf, size_t len, uint32_t timeoutMs) {
  int fd = c.fd();
  if (fd < 0) return 0;
  size_t sent = 0;
  unsigned long lastProgress = millis();
  while (sent < len) {
    fd_set wset, eset;
    FD_ZERO(&wset);
    FD_ZERO(&eset);
    FD_SET(fd, &wset);
    FD_SET(fd, &eset);
    struct timeval tv = {0, 100000};  // 100 ms slices
    int r = select(fd + 1, nullptr, &wset, &eset, &tv);
    if (r < 0 || FD_ISSET(fd, &eset)) break;  // socket error / reset
    if (r > 0 && FD_ISSET(fd, &wset)) {
      int n = send(fd, buf + sent, len - sent, MSG_DONTWAIT);
      if (n > 0) {
        sent += n;
        lastProgress = millis();
        continue;
      }
      if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) break;  // dead peer
    }
    if (millis() - lastProgress > timeoutMs) break;
    yield();
  }
  return sent;
}

void FyHttpServer::begin() {
  _server.begin();
  _server.setNoDelay(true);
  _running = true;
}

void FyHttpServer::stop() {
  for (auto &s : _slots) {
    if (s.used) s.c.stop();
    s.used = false;
  }
  _server.end();
  _running = false;
}

void FyHttpServer::on(const char *path, Handler fn) {
  for (int i = 0; i < _nRoutes; i++) {
    if (_routes[i].path == path) { _routes[i].fn = fn; return; }  // replace
  }
  if (_nRoutes < (int)(sizeof(_routes) / sizeof(_routes[0]))) {
    _routes[_nRoutes].path = path;
    _routes[_nRoutes].fn = fn;
    _nRoutes++;
  }
}

// True while the peer is still there. A reset/closed socket reports readable
// with recv() returning 0 or an error; select() exceptfds catches errors.
bool FyHttpServer::slotAlive(Slot &s) {
  int fd = s.c.fd();
  if (fd < 0) return false;
  fd_set rset, eset;
  FD_ZERO(&rset);
  FD_ZERO(&eset);
  FD_SET(fd, &rset);
  FD_SET(fd, &eset);
  struct timeval tv = {0, 0};
  int r = select(fd + 1, &rset, nullptr, &eset, &tv);
  if (r < 0 || FD_ISSET(fd, &eset)) return false;
  if (r > 0 && FD_ISSET(fd, &rset)) {
    char tmp;
    int n = recv(fd, &tmp, 1, MSG_PEEK | MSG_DONTWAIT);
    if (n == 0) return false;  // orderly close
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return false;
  }
  return true;
}

void FyHttpServer::handleClient() {
  if (!_running) return;

  // 1. Accept every pending connection into a free slot. If all slots are
  //    busy, recycle the one that has waited longest without a full request.
  for (;;) {
    WiFiClient nc = _server.available();
    if (!nc) break;
    int freeIdx = -1, oldest = -1;
    for (int i = 0; i < kSlots; i++) {
      if (!_slots[i].used) { freeIdx = i; break; }
      if (oldest < 0 || _slots[i].since < _slots[oldest].since) oldest = i;
    }
    if (freeIdx < 0) {
      _slots[oldest].c.stop();
      freeIdx = oldest;
    }
    Slot &s = _slots[freeIdx];
    s.c = nc;
    s.used = true;
    s.since = millis();
    s.len = 0;
  }

  // 2. Read whatever each connection has sent (non-blocking); drop dead or
  //    idle ones; dispatch the first complete request.
  int ready = -1;
  for (int i = 0; i < kSlots; i++) {
    Slot &s = _slots[i];
    if (!s.used) continue;
    int avail = s.c.available();
    if (avail > 0) {
      int room = kReqBuf - 1 - s.len;
      if (room > 0) {
        int n = s.c.read((uint8_t *)s.buf + s.len, min(avail, room));
        if (n > 0) s.len += n;
      } else {
        // headers too large: drain and let it complete/idle out
        uint8_t junk[64];
        s.c.read(junk, min(avail, (int)sizeof(junk)));
      }
      s.buf[s.len] = '\0';
    }
    bool complete = s.len >= 4 && strstr(s.buf, "\r\n\r\n") != nullptr;
    bool lineOnly = s.len == kReqBuf - 1 && strstr(s.buf, "\r\n") != nullptr;  // truncated headers
    if (complete || lineOnly) {
      if (ready < 0) ready = i;
      continue;
    }
    if (!slotAlive(s) || millis() - s.since > kIdleMs) {
      s.c.stop();
      s.used = false;
    }
  }
  if (ready >= 0) dispatch(_slots[ready]);
}

static String urlDecode(const String &in) {
  String out;
  out.reserve(in.length());
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '+') out += ' ';
    else if (c == '%' && i + 2 < in.length()) {
      char h[3] = {in[i + 1], in[i + 2], 0};
      out += (char)strtol(h, nullptr, 16);
      i += 2;
    } else out += c;
  }
  return out;
}

String FyHttpServer::arg(const char *name) const {
  size_t nlen = strlen(name);
  int pos = 0;
  while (pos <= (int)_query.length()) {
    int amp = _query.indexOf('&', pos);
    if (amp < 0) amp = _query.length();
    String kv = _query.substring(pos, amp);
    int eq = kv.indexOf('=');
    String k = eq < 0 ? kv : kv.substring(0, eq);
    if (k.length() == nlen && k == name) return urlDecode(eq < 0 ? String() : kv.substring(eq + 1));
    pos = amp + 1;
  }
  return String();
}

void FyHttpServer::dispatch(Slot &s) {
  // Request line: METHOD SP TARGET SP VERSION
  char *line = s.buf;
  char *sp1 = strchr(line, ' ');
  char *sp2 = sp1 ? strchr(sp1 + 1, ' ') : nullptr;
  char *eol = strstr(line, "\r\n");
  _cur = s.c;
  s.used = false;  // slot is free again; the handler owns the connection now
  _extraHeaders = "";
  if (!sp1 || !sp2 || !eol || sp2 > eol) {
    send(400, "text/plain", "bad request");
    return;
  }
  *sp1 = '\0';
  *sp2 = '\0';
  String method = line;
  String target = sp1 + 1;
  int q = target.indexOf('?');
  _uri = q < 0 ? target : target.substring(0, q);
  _query = q < 0 ? String() : target.substring(q + 1);

  if (method != "GET" && method != "HEAD") {
    send(405, "text/plain", "method not allowed");
    return;
  }
  for (int i = 0; i < _nRoutes; i++) {
    if (_routes[i].path == _uri) {
      _routes[i].fn();
      if (_cur.connected()) _cur.stop();
      return;
    }
  }
  if (_notFound) _notFound();
  else send(404, "text/plain", "not found");
  if (_cur.connected()) _cur.stop();
}

void FyHttpServer::sendHeader(const char *name, const String &value) {
  _extraHeaders += name;
  _extraHeaders += ": ";
  _extraHeaders += value;
  _extraHeaders += "\r\n";
}

void FyHttpServer::send(int code, const char *contentType, const String &body) {
  const char *reason = code == 200 ? "OK" : code == 204 ? "No Content" : code == 400 ? "Bad Request"
                     : code == 404 ? "Not Found" : code == 405 ? "Method Not Allowed" : "Error";
  String h = "HTTP/1.1 " + String(code) + " " + reason + "\r\n";
  if (code != 204) {
    h += "Content-Type: ";
    h += contentType;
    h += "\r\nContent-Length: " + String(body.length()) + "\r\n";
  }
  h += "Cache-Control: no-store\r\nConnection: close\r\n";
  h += _extraHeaders;
  h += "\r\n";
  _extraHeaders = "";
  size_t w = fyHttpWriteAll(_cur, (const uint8_t *)h.c_str(), h.length());
  if (w == h.length() && body.length())
    fyHttpWriteAll(_cur, (const uint8_t *)body.c_str(), body.length());
  _cur.stop();
}
