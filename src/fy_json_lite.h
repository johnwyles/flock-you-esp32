// flock-you-esp32 — tiny JSON object scanner for the web /table view
//
// Scope: find top-level JSON objects in a buffer (a JSON array OR JSON Lines)
// and walk each object's members in order, flattening nested objects into
// dotted keys ("gps.lat"). Values are returned decoded (string escapes
// resolved); arrays are returned as raw text. Plain C++ (std::string only),
// no Arduino dependencies, so it can be unit-tested on a host.
//
// Replaces the old hand-rolled quote scanner in fy_webserver.cpp, which
// re-read a string value's quotes as the next key and shifted every column
// to the right of the first string value.

#pragma once
#include <string>
#include <cstring>

namespace fyjson {

static inline void skipWs(const char *b, size_t n, size_t &i) {
  while (i < n && (b[i] == ' ' || b[i] == '\t' || b[i] == '\r' || b[i] == '\n')) i++;
}

// Parse a JSON string starting at b[i]=='"'. Leaves i after the closing quote.
static inline bool readString(const char *b, size_t n, size_t &i, std::string &out) {
  out.clear();
  if (i >= n || b[i] != '"') return false;
  i++;
  while (i < n) {
    char c = b[i++];
    if (c == '"') return true;
    if (c == '\\' && i < n) {
      char e = b[i++];
      switch (e) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'u':  // keep \uXXXX as '?' (ASCII-only display)
          i += (i + 4 <= n) ? 4 : (n - i);
          out += '?';
          break;
        default: out += e; break;  // \" \\ \/
      }
    } else {
      out += c;
    }
  }
  return false;
}

// Skip a balanced {...} or [...] starting at b[i]; leaves i after it.
static inline bool skipBalanced(const char *b, size_t n, size_t &i) {
  int depth = 0;
  bool inStr = false, esc = false;
  for (; i < n; i++) {
    char c = b[i];
    if (inStr) {
      if (esc) esc = false;
      else if (c == '\\') esc = true;
      else if (c == '"') inStr = false;
      continue;
    }
    if (c == '"') inStr = true;
    else if (c == '{' || c == '[') depth++;
    else if (c == '}' || c == ']') {
      if (--depth == 0) { i++; return true; }
    }
  }
  return false;
}

// Next top-level object at or after pos. Sets [s, e) and returns true.
static inline bool nextObject(const char *b, size_t n, size_t pos, size_t &s, size_t &e) {
  size_t i = pos;
  while (i < n && b[i] != '{') i++;
  if (i >= n) return false;
  s = i;
  if (!skipBalanced(b, n, i)) return false;
  e = i;
  return true;
}

// Walk the members of the object b[s, e). fn(const std::string &key,
// const std::string &value) is called for each scalar member; nested objects
// are flattened as "parent.child".
template <typename F>
static bool members(const char *b, size_t s, size_t e, F &fn, const std::string &prefix = "") {
  size_t i = s;
  if (i >= e || b[i] != '{') return false;
  i++;
  std::string key, val;
  while (i < e) {
    skipWs(b, e, i);
    if (i < e && b[i] == '}') return true;
    if (!readString(b, e, i, key)) return false;
    skipWs(b, e, i);
    if (i >= e || b[i] != ':') return false;
    i++;
    skipWs(b, e, i);
    if (i >= e) return false;
    std::string full = prefix.empty() ? key : prefix + "." + key;
    if (b[i] == '"') {
      if (!readString(b, e, i, val)) return false;
      fn(full, val);
    } else if (b[i] == '{') {
      size_t os = i;
      if (!skipBalanced(b, e, i)) return false;
      if (!members(b, os, i, fn, full)) return false;
    } else if (b[i] == '[') {
      size_t as = i;
      if (!skipBalanced(b, e, i)) return false;
      fn(full, std::string(b + as, i - as));
    } else {
      size_t vs = i;
      while (i < e && b[i] != ',' && b[i] != '}') i++;
      size_t ve = i;
      while (ve > vs && (b[ve - 1] == ' ' || b[ve - 1] == '\t' || b[ve - 1] == '\r' || b[ve - 1] == '\n')) ve--;
      fn(full, std::string(b + vs, ve - vs));
    }
    skipWs(b, e, i);
    if (i < e && b[i] == ',') { i++; continue; }
    if (i < e && b[i] == '}') return true;
  }
  return false;
}

}  // namespace fyjson
