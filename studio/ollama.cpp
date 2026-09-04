// Geekatplay TerraForge — minimal Ollama REST client (WinHTTP / BSD sockets)
#include "ollama.hpp"
#include <json.hpp>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

using json = nlohmann::json;

namespace studio {

#ifndef _WIN32
// Ollama is documented as a loopback service and speaks plain HTTP, so a
// socket is the entire client here: no TLS stack, no curl, nothing to ship.

// Chunked is the one framing Ollama can still choose for a non-streaming
// answer, and a body left in chunk framing is not JSON — it would come back
// as "bad Ollama response" with the size prefixes sitting in the text.
static bool dechunk(const std::string &in, std::string &out) {
  for (size_t i = 0; i < in.size();) {
    size_t eol = in.find("\r\n", i);
    if (eol == std::string::npos) return false;
    size_t len = 0;
    int digits = 0;
    for (size_t k = i; k < eol; ++k) {
      char c = in[k];
      int d;
      if (c >= '0' && c <= '9') d = c - '0';
      else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
      else break; // a ";chunk-extension" (or trailing space) ends the size
      len = len * 16 + (size_t)d;
      ++digits;
    }
    if (!digits) return false;
    i = eol + 2;
    if (!len) return true; // the terminating chunk; trailers are of no use here
    if (i + len > in.size()) return false;
    out.append(in, i, len);
    i += len + 2; // and the CRLF that closes the chunk
  }
  return false; // ran out of bytes before the terminating chunk
}

// connect() with a deadline. A blocking connect to a port nobody is listening
// on sits in the kernel for the system's own timeout — over a minute on a
// stalled host — and the thread waiting is the one the user is looking at.
static bool connect_timeout(int fd, const sockaddr *addr, socklen_t len, int ms) {
  int flags = ::fcntl(fd, F_GETFL, 0);
  ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  bool ok = ::connect(fd, addr, len) == 0;
  if (!ok && errno == EINPROGRESS) {
    pollfd p{fd, POLLOUT, 0};
    if (::poll(&p, 1, ms) > 0) {
      int soerr = 0;
      socklen_t n = sizeof soerr;
      ok = ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &n) == 0 && soerr == 0;
    }
  }
  ::fcntl(fd, F_SETFL, flags); // blocking again: SO_RCVTIMEO bounds the rest
  return ok;
}
#endif

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
  // host:port out of the url, the same way the Windows branch reads it
  std::string host = "127.0.0.1", port = "11434";
  {
    std::string u = url_utf8;
    size_t p = u.find("//");
    if (p != std::string::npos) u = u.substr(p + 2);
    size_t colon = u.find(':'), slash = u.find('/');
    std::string h = u.substr(0, std::min(colon, slash));
    if (!h.empty()) host = h;
    if (colon != std::string::npos && (slash == std::string::npos || colon < slash))
      port = u.substr(colon + 1,
                      slash == std::string::npos ? slash : slash - colon - 1);
  }
  // The one message the user can act on: everything below fails because the
  // server is not there, and naming the address says which one to start.
  const std::string unreachable =
      "cannot reach Ollama at " + url_utf8 + " (is `ollama serve` running?)";
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_UNSPEC; // localhost resolves to ::1 as often as 127.0.0.1
  hints.ai_socktype = SOCK_STREAM;
  if (::getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res) {
    err = unreachable;
    return false;
  }
  int fd = -1;
  for (addrinfo *ai = res; ai && fd < 0; ai = ai->ai_next) {
    int s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (s < 0) continue;
    if (connect_timeout(s, ai->ai_addr, (socklen_t)ai->ai_addrlen, 30000)) fd = s;
    else ::close(s);
  }
  ::freeaddrinfo(res);
  if (fd < 0) {
    err = unreachable;
    return false;
  }
  // A model thinking on the CPU can take minutes, so the read deadline is
  // generous; the send side only waits for the kernel's own buffer.
  timeval tv{600, 0};
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  tv.tv_sec = 30;
  ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
  std::string req = "POST " + path + " HTTP/1.1\r\nHost: " + host + ":" + port +
                    "\r\nContent-Type: application/json\r\nContent-Length: " +
                    std::to_string(body.size()) +
                    "\r\nConnection: close\r\n\r\n" + body;
  for (size_t sent = 0; sent < req.size();) {
    ssize_t w = ::send(fd, req.data() + sent, req.size() - sent, 0);
    if (w <= 0) {
      if (errno == EINTR) continue;
      ::close(fd);
      err = unreachable;
      return false;
    }
    sent += (size_t)w;
  }
  // "Connection: close" makes EOF the end of the message, so there is no
  // length to trust and nothing to guess at when the model streams slowly.
  std::string raw;
  char buf[16384];
  for (;;) {
    ssize_t got = ::recv(fd, buf, sizeof buf, 0);
    if (got > 0) {
      raw.append(buf, (size_t)got);
      continue;
    }
    if (got < 0 && errno == EINTR) continue;
    break;
  }
  ::close(fd);
  size_t head = raw.find("\r\n\r\n");
  if (head == std::string::npos) {
    err = raw.empty() ? unreachable : "Ollama closed the connection mid-reply";
    return false;
  }
  std::string headers = raw.substr(0, head);
  for (char &c : headers) c = (char)std::tolower((unsigned char)c);
  std::string payload = raw.substr(head + 4);
  if (headers.find("transfer-encoding: chunked") != std::string::npos) {
    std::string decoded;
    if (!dechunk(payload, decoded)) {
      err = "malformed chunked reply from Ollama";
      return false;
    }
    payload.swap(decoded);
  }
  response += payload; // the body only: the caller parses it as JSON
  return true;
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
