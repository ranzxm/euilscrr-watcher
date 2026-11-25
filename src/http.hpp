#pragma once

#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <ctime>
#include <optional>
#include <string>

#include "tracker.hpp"

#if __has_include("api_config.hpp")
#include "api_config.hpp"
#endif

#ifndef EUILSCRR_DEFAULT_API_URL
#define EUILSCRR_DEFAULT_API_URL L"https://euilscrr.vercel.app/api/watcher"
#endif

namespace euilscrr {

struct HttpResult {
  bool transport_ok = false;
  int status = 0;
  std::string body;
};

struct WinHttpHandle {
  HINTERNET h = nullptr;
  ~WinHttpHandle() {
    if (h) WinHttpCloseHandle(h);
  }
  explicit operator bool() const { return h != nullptr; }
};

inline std::wstring resolve_api_url() {
  wchar_t buf[512]{};
  DWORD len = GetEnvironmentVariableW(L"EUILSCRR_API_URL", buf, static_cast<DWORD>(std::size(buf)));
  if (len > 0 && len < std::size(buf)) {
    std::wstring candidate(buf, len);
    if (candidate.rfind(L"https://", 0) == 0 ||
        candidate.rfind(L"http://127.0.0.1", 0) == 0 ||
        candidate.rfind(L"http://localhost", 0) == 0) {
      return candidate;
    }
  }
  return EUILSCRR_DEFAULT_API_URL;
}

inline std::string json_escape(const std::string& input) {
  std::string out;
  out.reserve(input.size() + 8);
  for (unsigned char ch : input) {
    switch (ch) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (ch < 0x20) {
          char esc[8];
          std::snprintf(esc, sizeof(esc), "\\u%04x", static_cast<unsigned int>(ch));
          out += esc;
        } else {
          out.push_back(static_cast<char>(ch));
        }
        break;
    }
  }
  return out;
}

inline std::string format_iso_utc(int64_t epoch_ms) {
  if (epoch_ms < 0) epoch_ms = 0;
  time_t secs = static_cast<time_t>(epoch_ms / 1000);
  int ms = static_cast<int>(epoch_ms % 1000);
  struct tm utc{};
  gmtime_s(&utc, &secs);
  char buf[32];
  std::snprintf(
      buf,
      sizeof(buf),
      "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
      utc.tm_year + 1900,
      utc.tm_mon + 1,
      utc.tm_mday,
      utc.tm_hour,
      utc.tm_min,
      utc.tm_sec,
      ms);
  return buf;
}

inline std::string build_now_playing_json(
    const std::string& token,
    const std::optional<NowPlayingSnapshot>& np,
    std::optional<int64_t> adjusted_pos_ms) {
  std::string body = "{\"action\":\"now_playing\",\"token\":\"" + json_escape(token) + "\"";
  if (np.has_value()) {
    body += ",\"title\":\"" + json_escape(np->title) + "\"";
    body += ",\"artist\":\"" + json_escape(np->artist) + "\"";
    body += ",\"album\":\"" + json_escape(np->album) + "\"";
    if (np->duration_ms.has_value()) {
      body += ",\"duration_ms\":" + std::to_string(*np->duration_ms);
    }
    if (adjusted_pos_ms.has_value()) {
      body += ",\"position_ms\":" + std::to_string(*adjusted_pos_ms);
    }
    body += ",\"started_at\":\"" + format_iso_utc(np->started_at) + "\"";
    body += std::string(",\"is_playing\":") + (np->is_playing ? "true" : "false");
  }
  body += "}";
  return body;
}

inline std::string build_scrobble_json(const std::string& token, const ScrobbleEvent& sc) {
  std::string body = "{\"action\":\"scrobble\",\"token\":\"" + json_escape(token) + "\"";
  body += ",\"title\":\"" + json_escape(sc.title) + "\"";
  body += ",\"artist\":\"" + json_escape(sc.artist) + "\"";
  body += ",\"album\":\"" + json_escape(sc.album) + "\"";
  if (sc.duration_ms.has_value()) {
    body += ",\"duration_ms\":" + std::to_string(*sc.duration_ms);
  }
  body += ",\"played_at\":\"" + format_iso_utc(sc.played_at) + "\"}";
  return body;
}

inline HttpResult post_json(const std::wstring& url, const std::string& json_body, int timeout_ms = 10000) {
  HttpResult res;
  URL_COMPONENTSW parts{};
  parts.dwStructSize = sizeof(parts);
  wchar_t host[256]{};
  wchar_t path[1024]{};
  parts.lpszHostName = host;
  parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
  parts.lpszUrlPath = path;
  parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));

  if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts)) {
    return res;
  }

  WinHttpHandle session{WinHttpOpen(
      L"euilscrr-watcher/1.0",
      WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
      WINHTTP_NO_PROXY_NAME,
      WINHTTP_NO_PROXY_BYPASS,
      0)};
  if (!session) return res;

  WinHttpSetTimeouts(session.h, 5000, 5000, timeout_ms, timeout_ms);

  WinHttpHandle conn{WinHttpConnect(session.h, host, parts.nPort, 0)};
  if (!conn) return res;

  DWORD flags = (parts.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
  WinHttpHandle req{WinHttpOpenRequest(
      conn.h,
      L"POST",
      path[0] ? path : L"/",
      nullptr,
      WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES,
      flags)};
  if (!req) return res;

  const wchar_t* headers = L"Content-Type: application/json; charset=utf-8\r\n";
  BOOL sent = WinHttpSendRequest(
      req.h,
      headers,
      static_cast<DWORD>(-1),
      const_cast<char*>(json_body.data()),
      static_cast<DWORD>(json_body.size()),
      static_cast<DWORD>(json_body.size()),
      0);
  if (!sent || !WinHttpReceiveResponse(req.h, nullptr)) {
    return res;
  }

  DWORD status_code = 0;
  DWORD status_size = sizeof(status_code);
  if (!WinHttpQueryHeaders(
          req.h,
          WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
          WINHTTP_HEADER_NAME_BY_INDEX,
          &status_code,
          &status_size,
          WINHTTP_NO_HEADER_INDEX)) {
    return res;
  }

  res.transport_ok = true;
  res.status = static_cast<int>(status_code);

  DWORD avail = 0;
  while (WinHttpQueryDataAvailable(req.h, &avail) && avail > 0) {
    if (res.body.size() >= 4096) break;
    DWORD to_read = (avail > 1024) ? 1024 : avail;
    char chunk[1024];
    DWORD read = 0;
    if (!WinHttpReadData(req.h, chunk, to_read, &read) || read == 0) break;
    res.body.append(chunk, read);
  }

  return res;
}

}  // namespace euilscrr
