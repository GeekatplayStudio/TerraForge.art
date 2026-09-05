// Geekatplay TerraForge - one HTTP client for every service the studio
// talks to: Ollama, ComfyUI, and the cloud providers behind the AI menus.
//
// WinHTTP on Windows (TLS included, no dependency); POSIX sockets elsewhere
// for plain http, which is what a local Ollama or ComfyUI speaks. Blocking
// by design - the callers run on the job thread (ai_jobs.cpp), never on the
// UI thread - with a connect deadline so a server that is not there fails
// in seconds, not minutes.
#pragma once
#include <map>
#include <string>
#include <vector>

namespace studio {

struct HttpResponse {
  int status = 0;        // 0 when the request never reached a server
  std::string body;      // bytes as received
  std::string error;     // why status is 0, or the server's own error text
  bool ok() const { return status >= 200 && status < 300; }
};

struct HttpRequest {
  std::string method = "GET";
  std::string url;                              // full: scheme://host[:port]/path?query
  std::map<std::string, std::string> headers;   // Authorization, Content-Type...
  std::string body;                             // sent as-is
  int connect_ms = 8000;
  int receive_ms = 600000;                      // a model can think for minutes
};

// The request, done. Never throws.
HttpResponse http_send(const HttpRequest &req);

// Shorthands: JSON in, JSON-or-text out.
HttpResponse http_get(const std::string &url, const std::map<std::string, std::string> &headers = {});
HttpResponse http_post_json(const std::string &url, const std::string &json_body,
                            const std::map<std::string, std::string> &headers = {});

// A multipart/form-data body from fields and files; sets the boundary in
// `content_type`.
struct FormPart {
  std::string name;
  std::string value;     // text field, or file bytes when `filename` is set
  std::string filename;  // empty for a text field
  std::string mime;      // for a file
};
std::string http_multipart(const std::vector<FormPart> &parts, std::string &content_type);

// Fetch a URL to a file (a provider's result image or model). False with
// `err` set when the server refused or the file could not be written.
bool http_download(const std::string &url, const std::string &out_path, std::string &err,
                   const std::map<std::string, std::string> &headers = {});

// URL pieces the client and the services both need.
struct UrlParts {
  std::string scheme, host, path; // path includes the query
  int port = 80;
  bool tls = false;
};
bool url_split(const std::string &url, UrlParts &out);

std::string base64_encode(const std::string &bytes);
std::string base64_decode(const std::string &text);

} // namespace studio
