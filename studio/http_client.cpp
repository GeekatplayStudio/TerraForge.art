// Geekatplay TerraForge - the HTTP client. See http_client.hpp.
#include "http_client.hpp"
#include <cstring>
#include <fstream>
#include <random>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace studio {

bool url_split(const std::string &url, UrlParts &o) {
  size_t p = url.find("://");
  if (p == std::string::npos) return false;
  o.scheme = url.substr(0, p);
  o.tls = o.scheme == "https";
  o.port = o.tls ? 443 : 80;
  std::string rest = url.substr(p + 3);
  size_t slash = rest.find('/');
  std::string hostport = rest.substr(0, slash);
  o.path = slash == std::string::npos ? "/" : rest.substr(slash);
  size_t colon = hostport.find(':');
  if (colon != std::string::npos) {
    o.host = hostport.substr(0, colon);
    o.port = std::atoi(hostport.c_str() + colon + 1);
  } else {
    o.host = hostport;
  }
  return !o.host.empty();
}

// ---------------------------------------------------------------- base64
static const char *const B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const std::string &in) {
  std::string out;
  int val = 0, bits = -6;
  for (unsigned char c : in) {
    val = (val << 8) + c;
    bits += 8;
    while (bits >= 0) {
      out.push_back(B64[(val >> bits) & 0x3F]);
      bits -= 6;
    }
  }
  if (bits > -6) out.push_back(B64[((val << 8) >> (bits + 8)) & 0x3F]);
  while (out.size() % 4) out.push_back('=');
  return out;
}

std::string base64_decode(const std::string &in) {
  std::string out;
  std::vector<int> T(256, -1);
  for (int i = 0; i < 64; i++) T[(unsigned char)B64[i]] = i;
  int val = 0, bits = -8;
  for (unsigned char c : in) {
    if (T[c] == -1) {
      if (c == '=') break;
      continue; // newlines in a wrapped payload
    }
    val = (val << 6) + T[c];
    bits += 6;
    if (bits >= 0) {
      out.push_back(char((val >> bits) & 0xFF));
      bits -= 8;
    }
  }
  return out;
}

std::string http_multipart(const std::vector<FormPart> &parts, std::string &content_type) {
  std::string boundary = "----TerraForge";
  std::mt19937 rng((unsigned)std::random_device{}());
  for (int i = 0; i < 16; ++i) boundary.push_back("abcdefghijklmnopqrstuvwxyz0123456789"[rng() % 36]);
  content_type = "multipart/form-data; boundary=" + boundary;
  std::string b;
  for (const FormPart &p : parts) {
    b += "--" + boundary + "\r\n";
    b += "Content-Disposition: form-data; name=\"" + p.name + "\"";
    if (!p.filename.empty()) b += "; filename=\"" + p.filename + "\"";
    b += "\r\n";
    if (!p.filename.empty())
      b += "Content-Type: " + (p.mime.empty() ? std::string("application/octet-stream") : p.mime) + "\r\n";
    b += "\r\n" + p.value + "\r\n";
  }
  b += "--" + boundary + "--\r\n";
  return b;
}

// ------------------------------------------------------------------ send
#ifdef _WIN32
static std::wstring widen(const std::string &s) {
  if (s.empty()) return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
  std::wstring w((size_t)n, 0);
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
  return w;
}

HttpResponse http_send(const HttpRequest &rq) {
  HttpResponse r;
  UrlParts u;
  if (!url_split(rq.url, u)) {
    r.error = "bad url: " + rq.url;
    return r;
  }
  HINTERNET session = WinHttpOpen(L"GeekatplayTerraForge/2.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) {
    r.error = "WinHttpOpen failed";
    return r;
  }
  WinHttpSetTimeouts(session, rq.connect_ms, rq.connect_ms, 30000, rq.receive_ms);
  HINTERNET conn = WinHttpConnect(session, widen(u.host).c_str(), (INTERNET_PORT)u.port, 0);
  if (!conn) {
    r.error = "cannot connect to " + u.host;
    WinHttpCloseHandle(session);
    return r;
  }
  HINTERNET req = WinHttpOpenRequest(conn, widen(rq.method).c_str(), widen(u.path).c_str(), nullptr,
                                     WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     u.tls ? WINHTTP_FLAG_SECURE : 0);
  if (!req) {
    r.error = "cannot open request";
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return r;
  }
  std::wstring hdrs;
  for (const auto &kv : rq.headers) hdrs += widen(kv.first + ": " + kv.second + "\r\n");
  const wchar_t *h = hdrs.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdrs.c_str();
  if (WinHttpSendRequest(req, h, hdrs.empty() ? 0 : (DWORD)-1, (LPVOID)rq.body.data(),
                         (DWORD)rq.body.size(), (DWORD)rq.body.size(), 0) &&
      WinHttpReceiveResponse(req, nullptr)) {
    DWORD status = 0, sz = sizeof status;
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    r.status = (int)status;
    DWORD avail = 0;
    do {
      avail = 0;
      if (!WinHttpQueryDataAvailable(req, &avail) || !avail) break;
      std::vector<char> buf(avail);
      DWORD got = 0;
      WinHttpReadData(req, buf.data(), avail, &got);
      r.body.append(buf.data(), got);
    } while (avail > 0);
    if (!r.ok()) r.error = "HTTP " + std::to_string(r.status);
  } else {
    DWORD e = GetLastError();
    r.error = e == ERROR_WINHTTP_TIMEOUT          ? "timed out"
              : e == ERROR_WINHTTP_CANNOT_CONNECT ? "cannot connect to " + u.host + ":" + std::to_string(u.port)
                                                  : "request failed (" + std::to_string(e) + ")";
  }
  WinHttpCloseHandle(req);
  WinHttpCloseHandle(conn);
  WinHttpCloseHandle(session);
  return r;
}
#else
// POSIX: plain http only, enough for the local services
HttpResponse http_send(const HttpRequest &rq) {
  HttpResponse r;
  UrlParts u;
  if (!url_split(rq.url, u)) { r.error = "bad url"; return r; }
  if (u.tls) { r.error = "https needs the Windows build or a proxy"; return r; }
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(u.host.c_str(), std::to_string(u.port).c_str(), &hints, &res) != 0 || !res) {
    r.error = "cannot resolve " + u.host;
    return r;
  }
  int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  int flags = ::fcntl(fd, F_GETFL, 0);
  ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  bool ok = ::connect(fd, res->ai_addr, res->ai_addrlen) == 0;
  if (!ok && errno == EINPROGRESS) {
    pollfd p{fd, POLLOUT, 0};
    if (::poll(&p, 1, rq.connect_ms) > 0) {
      int soerr = 0;
      socklen_t n = sizeof soerr;
      ok = ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &n) == 0 && soerr == 0;
    }
  }
  ::fcntl(fd, F_SETFL, flags);
  freeaddrinfo(res);
  if (!ok) { ::close(fd); r.error = "cannot connect to " + u.host; return r; }
  timeval tv{rq.receive_ms / 1000, 0};
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  std::string msg = rq.method + " " + u.path + " HTTP/1.1\r\nHost: " + u.host + "\r\nConnection: close\r\n";
  for (const auto &kv : rq.headers) msg += kv.first + ": " + kv.second + "\r\n";
  msg += "Content-Length: " + std::to_string(rq.body.size()) + "\r\n\r\n" + rq.body;
  ::send(fd, msg.data(), msg.size(), 0);
  std::string raw;
  char buf[16384];
  for (;;) {
    ssize_t n = ::recv(fd, buf, sizeof buf, 0);
    if (n <= 0) break;
    raw.append(buf, (size_t)n);
  }
  ::close(fd);
  size_t hdr_end = raw.find("\r\n\r\n");
  if (hdr_end == std::string::npos) { r.error = "no response"; return r; }
  r.status = std::atoi(raw.c_str() + 9);
  std::string headers = raw.substr(0, hdr_end), body = raw.substr(hdr_end + 4);
  if (headers.find("chunked") != std::string::npos) {
    std::string out; size_t pos = 0;
    while (pos < body.size()) {
      size_t eol = body.find("\r\n", pos);
      if (eol == std::string::npos) break;
      size_t len = std::strtoul(body.substr(pos, eol - pos).c_str(), nullptr, 16);
      if (!len) break;
      out.append(body, eol + 2, len);
      pos = eol + 2 + len + 2;
    }
    body = out;
  }
  r.body = body;
  if (!r.ok()) r.error = "HTTP " + std::to_string(r.status);
  return r;
}
#endif

HttpResponse http_get(const std::string &url, const std::map<std::string, std::string> &headers) {
  HttpRequest rq;
  rq.url = url;
  rq.headers = headers;
  return http_send(rq);
}

HttpResponse http_post_json(const std::string &url, const std::string &body,
                            const std::map<std::string, std::string> &headers) {
  HttpRequest rq;
  rq.method = "POST";
  rq.url = url;
  rq.headers = headers;
  rq.headers["Content-Type"] = "application/json";
  rq.body = body;
  return http_send(rq);
}

bool http_download(const std::string &url, const std::string &out_path, std::string &err,
                   const std::map<std::string, std::string> &headers) {
  HttpResponse r = http_get(url, headers);
  if (!r.ok()) {
    err = r.error.empty() ? "download failed" : r.error;
    return false;
  }
  std::ofstream f(out_path, std::ios::binary);
  if (!f) {
    err = "cannot write " + out_path;
    return false;
  }
  f.write(r.body.data(), (std::streamsize)r.body.size());
  return (bool)f;
}

} // namespace studio
