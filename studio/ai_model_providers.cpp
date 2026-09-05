// Geekatplay TerraForge - 3D models from Meshy, Tripo and Hitem3D. Each is
// submit-then-poll; the status parsers turn three different answers into
// one TaskStatus, and the result file is downloaded before success is
// reported. Meshy's text-to-3D is two stages (preview, then refine for
// textures), handled here so the caller sees one task.
#include "ai_services.hpp"
#include "config.hpp"
#include "http_client.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <sstream>
#include <thread>

using nlohmann::json;
namespace fs = std::filesystem;

namespace studio {

// ---------------------------------------------------------------- builders
std::string meshy_text_json(const std::string &prompt, const std::string &model) {
  json j = {{"mode", "preview"}, {"prompt", prompt}, {"art_style", "realistic"},
            {"ai_model", model.empty() ? "meshy-4" : model}, {"topology", "quad"}, {"should_remesh", true}};
  return j.dump();
}

std::string tripo_text_json(const std::string &prompt) {
  json j = {{"type", "text_to_model"}, {"prompt", prompt}};
  return j.dump();
}

std::string hitems_auth_header(const std::string &key) {
  if (key.rfind("Bearer ", 0) == 0 || key.rfind("Basic ", 0) == 0) return key;
  if (key.find(':') != std::string::npos) return "Basic " + base64_encode(key);
  return "Bearer " + key;
}

// ----------------------------------------------------------------- parsers
static float progress_of(const json &v) {
  if (!v.is_number()) return 0.f;
  double p = v.get<double>();
  if (p <= 1.0) p *= 100.0;
  return (float)std::max(0.0, std::min(100.0, p)) / 100.f;
}

bool parse_meshy_status(const std::string &body, TaskStatus &st) {
  json j = json::parse(body, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return false;
  if (j.contains("message") && !j.contains("status")) { st.state = TaskStatus::FAILED; st.message = j["message"].get<std::string>(); return true; }
  std::string s = j.value("status", "PENDING");
  for (char &c : s) c = (char)toupper((unsigned char)c);
  st.task_id = j.value("id", "");
  st.progress = progress_of(j.value("progress", json(0)));
  st.thumb_url = j.value("thumbnail_url", "");
  if (s == "SUCCEEDED") {
    st.state = TaskStatus::SUCCEEDED;
    const json &urls = j.value("model_urls", json::object());
    st.result_url = urls.value("glb", urls.value("obj", ""));
  } else if (s == "FAILED" || s == "EXPIRED" || s == "CANCELED" || s == "CANCELLED") {
    st.state = TaskStatus::FAILED;
    st.message = j.value("task_error", json::object()).value("message", s);
  } else if (s == "IN_PROGRESS") {
    st.state = TaskStatus::RUNNING;
  } else {
    st.state = TaskStatus::PENDING;
  }
  return true;
}

bool parse_tripo_status(const std::string &body, TaskStatus &st) {
  json j = json::parse(body, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return false;
  if (j.value("code", 0) != 0 && !j.contains("data")) { st.state = TaskStatus::FAILED; st.message = j.value("message", "error"); return true; }
  const json &d = j.value("data", json::object());
  std::string s = d.value("status", "queued");
  for (char &c : s) c = (char)tolower((unsigned char)c);
  st.task_id = d.value("task_id", "");
  st.progress = progress_of(d.value("progress", json(0)));
  const json &out = d.value("output", json::object());
  st.thumb_url = out.value("rendered_image", out.value("render_image", ""));
  if (s == "success") {
    st.state = TaskStatus::SUCCEEDED;
    st.result_url = out.value("model", out.value("pbr_model", out.value("base_model", "")));
  } else if (s == "failed" || s == "cancelled" || s == "banned" || s == "expired") {
    st.state = TaskStatus::FAILED;
    st.message = s;
  } else if (s == "running") {
    st.state = TaskStatus::RUNNING;
  } else {
    st.state = TaskStatus::PENDING;
  }
  return true;
}

bool parse_hitems_status(const std::string &body, TaskStatus &st) {
  json j = json::parse(body, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return false;
  const json &d = j.contains("data") && j["data"].is_object() ? j["data"] : j;
  st.task_id = d.value("task_id", "");
  int status = d.value("task_status", -100);
  std::string state = d.value("state", "");
  for (const char *k : {"process_pct", "progress", "process", "percentage", "percent"})
    if (d.contains(k) && d[k].is_number()) { st.progress = progress_of(d[k]); break; }
  const json &res = d.value("task_result", json::object());
  st.result_url = res.value("model_url", res.value("url", d.value("model_url", d.value("url", ""))));
  st.thumb_url = res.value("render_url", res.value("cover_url", ""));
  if (status == 4 || state == "success" || !st.result_url.empty()) st.state = TaskStatus::SUCCEEDED;
  else if (status == -1 || state == "failed") { st.state = TaskStatus::FAILED; st.message = d.value("msg", j.value("message", "failed")); }
  else if (state == "processing" || state == "running") st.state = TaskStatus::RUNNING;
  else st.state = TaskStatus::PENDING;
  return true;
}

// --------------------------------------------------------------------- run
namespace {

std::string read_file(const std::string &p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

bool wait_task(const std::string &poll_url, const std::map<std::string, std::string> &headers,
               bool (*parse)(const std::string &, TaskStatus &), std::atomic<bool> *cancel,
               std::function<void(float, const std::string &)> progress, TaskStatus &st, std::string &err) {
  int delay = 0;
  float last = -1.f;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::hours(2);
  int errors = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    if (cancel && cancel->load()) { err = "cancelled"; return false; }
    delay = poll_next_delay_ms(delay, st.progress > last);
    last = st.progress;
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    HttpResponse r = http_get(poll_url, headers);
    if (r.status == 0 || !parse(r.body, st)) {
      if (++errors >= 5) { err = r.error.empty() ? "the provider stopped answering" : r.error; return false; }
      continue;
    }
    errors = 0;
    if (progress) progress(0.1f + 0.8f * st.progress, "generating (" + std::to_string((int)(st.progress * 100)) + "%)");
    if (st.state == TaskStatus::SUCCEEDED) return true;
    if (st.state == TaskStatus::FAILED) { err = st.message.empty() ? "the provider failed the task" : st.message; return false; }
  }
  err = "timed out after two hours";
  return false;
}

std::string task_id_of(const std::string &body) {
  json j = json::parse(body, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return "";
  if (j.contains("result") && j["result"].is_string()) return j["result"].get<std::string>();
  if (j.contains("id") && j["id"].is_string()) return j["id"].get<std::string>();
  const json &d = j.value("data", json::object());
  if (d.is_object()) return d.value("task_id", "");
  return "";
}

} // namespace

GenResult ai_generate_model(const GenModelRequest &req, std::atomic<bool> *cancel,
                            std::function<void(float, const std::string &)> progress) {
  GenResult res;
  std::string provider = req.provider.empty() ? config().ai.model_provider : req.provider;
  if (!service_ready(provider)) { res.message = provider + " is not configured (Settings > AI services)"; return res; }
  ServiceConfig s = service_resolved(provider);
  std::string err;
  TaskStatus st;
  if (progress) progress(0.05f, "submitting to " + provider);
  std::error_code ec;
  fs::create_directories(req.out_dir, ec);

  if (provider == "meshy") {
    std::map<std::string, std::string> h = {{"Authorization", "Bearer " + s.key}};
    std::string base = s.endpoint;
    std::string kind = req.image_path.empty() ? "text-to-3d" : "image-to-3d";
    std::string body;
    if (req.image_path.empty()) body = meshy_text_json(req.prompt, s.model);
    else body = json{{"image_url", "data:image/png;base64," + base64_encode(read_file(req.image_path))},
                     {"enable_pbr", req.pbr}, {"should_texture", true}, {"should_remesh", true}}.dump();
    std::string url = base + (kind == "text-to-3d" ? "/v2/text-to-3d" : "/v1/image-to-3d");
    HttpResponse r = http_post_json(url, body, h);
    if (r.status == 0) { res.message = r.error; return res; }
    std::string id = task_id_of(r.body);
    if (id.empty()) { res.message = "Meshy gave no task id: " + r.body.substr(0, 200); return res; }
    if (!wait_task(url + "/" + id, h, parse_meshy_status, cancel, progress, st, err)) { res.message = err; return res; }
    if (kind == "text-to-3d") {
      // the preview is geometry only; refine for textures
      if (progress) progress(0.5f, "refining textures");
      HttpResponse r2 = http_post_json(url, json{{"mode", "refine"}, {"preview_task_id", id}, {"enable_pbr", req.pbr}}.dump(), h);
      std::string id2 = task_id_of(r2.body);
      if (!id2.empty()) {
        TaskStatus st2;
        if (wait_task(url + "/" + id2, h, parse_meshy_status, cancel, progress, st2, err)) st = st2;
      }
    }
  } else if (provider == "tripo") {
    std::map<std::string, std::string> h = {{"Authorization", "Bearer " + s.key}};
    std::string body;
    if (req.image_path.empty()) body = tripo_text_json(req.prompt);
    else {
      std::string ct;
      HttpRequest up;
      up.method = "POST";
      up.url = s.endpoint + "/upload";
      up.body = http_multipart({{"file", read_file(req.image_path), "image.png", "image/png"}}, ct);
      up.headers = {{"Authorization", "Bearer " + s.key}, {"Content-Type", ct}};
      HttpResponse ur = http_send(up);
      json uj = json::parse(ur.body, nullptr, false);
      std::string token = uj.is_object() ? uj.value("data", json::object()).value("image_token", "") : "";
      if (token.empty()) { res.message = "Tripo upload failed: " + ur.error; return res; }
      body = json{{"type", "image_to_model"}, {"file", {{"type", "png"}, {"file_token", token}}}}.dump();
    }
    HttpResponse r = http_post_json(s.endpoint + "/task", body, h);
    if (r.status == 0) { res.message = r.error; return res; }
    std::string id = task_id_of(r.body);
    if (id.empty()) { res.message = "Tripo gave no task id: " + r.body.substr(0, 200); return res; }
    if (!wait_task(s.endpoint + "/task/" + id, h, parse_tripo_status, cancel, progress, st, err)) { res.message = err; return res; }
  } else if (provider == "hitem3d") {
    if (req.image_path.empty()) { res.message = "Hitem3D builds from an image; add a reference picture"; return res; }
    std::string auth = hitems_auth_header(s.key);
    if (auth.rfind("Basic ", 0) == 0) {
      HttpResponse t = http_post_json(s.endpoint + "/auth/token", "{}", {{"Authorization", auth}, {"Accept", "application/json"}});
      json tj = json::parse(t.body, nullptr, false);
      std::string tok;
      if (tj.is_object()) {
        const json &d = tj.contains("data") ? tj["data"] : tj;
        tok = d.value("accessToken", d.value("access_token", d.value("token", "")));
      }
      if (tok.empty()) { res.message = "Hitem3D token exchange failed: " + t.error; return res; }
      auth = "Bearer " + tok;
    }
    std::string ct;
    HttpRequest sub;
    sub.method = "POST";
    sub.url = s.endpoint + "/submit-task";
    sub.body = http_multipart({{"images", read_file(req.image_path), "front.png", "image/png"},
                               {"model", s.model.empty() ? "hitem3dv1.5" : s.model, "", ""},
                               {"request_type", "3", "", ""},
                               {"resolution", "1024", "", ""},
                               {"format", "1", "", ""}}, ct); // 1 = OBJ, which we read natively
    sub.headers = {{"Authorization", auth}, {"Content-Type", ct}};
    HttpResponse r = http_send(sub);
    if (r.status == 0) { res.message = r.error; return res; }
    std::string id = task_id_of(r.body);
    if (id.empty()) { res.message = "Hitem3D gave no task id: " + r.body.substr(0, 200); return res; }
    if (!wait_task(s.endpoint + "/query-task?task_id=" + id, {{"Authorization", auth}}, parse_hitems_status, cancel, progress, st, err)) { res.message = err; return res; }
  } else {
    res.message = "no 3D adapter for " + provider;
    return res;
  }
  if (st.result_url.empty()) { res.message = "the provider reported success without a model"; return res; }
  if (progress) progress(0.95f, "downloading the model");
  std::string ext = st.result_url.find(".obj") != std::string::npos ? ".obj" : ".glb";
  std::string out = (fs::path(req.out_dir) / ("ai_model_" + std::to_string((long long)std::chrono::system_clock::now().time_since_epoch().count() / 1000000) + ext)).string();
  if (!http_download(st.result_url, out, err)) { res.message = err; return res; }
  if (!st.thumb_url.empty()) {
    std::string thumb = out.substr(0, out.size() - 4) + ".png";
    std::string e2;
    http_download(st.thumb_url, thumb, e2);
  }
  if (progress) progress(1.f, "done");
  res.ok = true;
  res.path = out;
  return res;
}

} // namespace studio
