// Geekatplay TerraForge — minimal Ollama REST client (WinHTTP)
#include "ollama.hpp"
#include <json.hpp>
#include <cstdio>
#include <fstream>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#endif

using json = nlohmann::json;

namespace studio {

static bool http_post_json(const std::string &url_utf8, const std::string &path,
                           const std::string &body, std::string &response,
                           std::string &err) {
#ifdef _WIN32
  // parse host:port from url like http://127.0.0.1:11434
  std::wstring host;
  INTERNET_PORT port = 11434;
  {
    std::string u = url_utf8;
    size_t p = u.find("//");
    if (p != std::string::npos) u = u.substr(p + 2);
    size_t colon = u.find(':');
    size_t slash = u.find('/');
    std::string h = u.substr(0, std::min(colon, slash));
    if (colon != std::string::npos && (slash == std::string::npos || colon < slash))
      port = (INTERNET_PORT)std::stoi(u.substr(colon + 1));
    host.assign(h.begin(), h.end());
  }
  HINTERNET session = WinHttpOpen(L"GeekatplayTerraForge/2.0",
                                  WINHTTP_ACCESS_TYPE_NO_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) {
    err = "WinHttpOpen failed";
    return false;
  }
  HINTERNET conn = WinHttpConnect(session, host.c_str(), port, 0);
  if (!conn) {
    err = "cannot connect to " + url_utf8;
    WinHttpCloseHandle(session);
    return false;
  }
  std::wstring wpath(path.begin(), path.end());
  HINTERNET req = WinHttpOpenRequest(conn, L"POST", wpath.c_str(), nullptr,
                                     WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
  bool ok = false;
  if (req) {
    // model responses can take minutes on CPU
    DWORD timeout = 600000;
    WinHttpSetTimeouts(req, 30000, 30000, timeout, timeout);
    const wchar_t *hdrs = L"Content-Type: application/json\r\n";
    if (WinHttpSendRequest(req, hdrs, (DWORD)-1, (LPVOID)body.data(),
                           (DWORD)body.size(), (DWORD)body.size(), 0) &&
        WinHttpReceiveResponse(req, nullptr)) {
      DWORD avail = 0;
      do {
        avail = 0;
        WinHttpQueryDataAvailable(req, &avail);
        if (!avail) break;
        std::vector<char> buf(avail);
        DWORD got = 0;
        WinHttpReadData(req, buf.data(), avail, &got);
        response.append(buf.data(), got);
      } while (avail > 0);
      ok = true;
    } else {
      err = "request failed (is Ollama running?)";
    }
    WinHttpCloseHandle(req);
  }
  WinHttpCloseHandle(conn);
  WinHttpCloseHandle(session);
  return ok;
#else
  (void)url_utf8; (void)path; (void)body; (void)response;
  err = "HTTP client not implemented on this platform";
  return false;
#endif
}

static std::string base64_encode(const std::vector<unsigned char> &data) {
  static const char tbl[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((data.size() + 2) / 3 * 4);
  for (size_t i = 0; i < data.size(); i += 3) {
    unsigned v = data[i] << 16;
    if (i + 1 < data.size()) v |= data[i + 1] << 8;
    if (i + 2 < data.size()) v |= data[i + 2];
    out += tbl[(v >> 18) & 63];
    out += tbl[(v >> 12) & 63];
    out += i + 1 < data.size() ? tbl[(v >> 6) & 63] : '=';
    out += i + 2 < data.size() ? tbl[v & 63] : '=';
  }
  return out;
}

bool ollama_generate(const std::string &url, const std::string &model,
                     const std::string &system, const std::string &prompt,
                     const std::string &image_path, std::string &out_text,
                     std::string &err) {
  json body;
  body["model"] = model;
  body["system"] = system;
  body["prompt"] = prompt;
  body["stream"] = false;
  body["format"] = "json";
  body["options"] = {{"temperature", 0.4}, {"num_predict", 4096}};
  if (!image_path.empty()) {
    std::ifstream f(image_path, std::ios::binary);
    if (!f) {
      err = "cannot open image: " + image_path;
      return false;
    }
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
    body["images"] = {base64_encode(data)};
  }
  std::string resp;
  if (!http_post_json(url, "/api/generate", body.dump(), resp, err)) return false;
  try {
    json j = json::parse(resp);
    if (j.contains("error")) {
      err = j["error"].get<std::string>();
      return false;
    }
    out_text = j.value("response", "");
    if (out_text.empty()) {
      err = "empty model response";
      return false;
    }
    return true;
  } catch (const std::exception &e) {
    err = std::string("bad Ollama response: ") + e.what();
    return false;
  }
}

bool ollama_available(const std::string &url) {
  std::string resp, err;
  // /api/tags is a GET; a POST with empty body still answers on Ollama,
  // but use version endpoint via POST-tolerant generate check instead:
  return http_post_json(url, "/api/show", "{}", resp, err);
}

} // namespace studio
