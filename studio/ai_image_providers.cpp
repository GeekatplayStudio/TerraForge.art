// Geekatplay TerraForge - images from the cloud providers: OpenAI Images,
// Stability AI (v2beta core), Google Imagen. Each adapter is the documented
// wire format and nothing else; ComfyUI has its own file (ai_comfy.cpp).
// ai_generate_image() picks the adapter by provider id, wraps the prompt
// for the kind of image (texture, skydome), writes the PNG to disk and
// only then reports success.
#include "ai_services.hpp"
#include "config.hpp"
#include "http_client.hpp"
#include <cmath>
#include <fstream>
#include <json.hpp>

using nlohmann::json;

namespace studio {

// ------------------------------------------------------------------ OpenAI
std::string openai_size_for(int w, int h, const std::string &model) {
  float ratio = h > 0 ? (float)w / (float)h : 1.f;
  if (model.rfind("gpt-image", 0) == 0) {
    if (ratio >= 1.3f) return "1536x1024";
    if (ratio <= 0.7f) return "1024x1536";
    return "1024x1024";
  }
  if (ratio >= 1.3f) return "1792x1024";
  if (ratio <= 0.7f) return "1024x1792";
  return "1024x1024";
}

std::string openai_image_json(const std::string &model, const std::string &prompt, int w, int h) {
  json j = {{"model", model}, {"prompt", prompt}, {"n", 1}, {"size", openai_size_for(w, h, model)}};
  // gpt-image returns base64 always; dall-e needs to be asked
  if (model.rfind("gpt-image", 0) != 0) j["response_format"] = "b64_json";
  return j.dump();
}

bool parse_openai_image(const std::string &body, std::string &png, std::string &err) {
  json j = json::parse(body, nullptr, false);
  if (j.is_discarded()) { err = "not JSON"; return false; }
  if (j.contains("error")) { err = j["error"].value("message", "error"); return false; }
  if (!j.contains("data") || !j["data"].is_array() || j["data"].empty()) { err = "no image data"; return false; }
  const json &d = j["data"][0];
  if (d.contains("b64_json") && d["b64_json"].is_string()) {
    png = base64_decode(d["b64_json"].get<std::string>());
    return !png.empty();
  }
  if (d.contains("url") && d["url"].is_string()) {
    png = "url:" + d["url"].get<std::string>();
    return true;
  }
  err = "neither b64_json nor url in the reply";
  return false;
}

// --------------------------------------------------------------- Stability
void stability_snap_size(int &w, int &h) {
  static const int legal[9][2] = {{1024, 1024}, {1152, 896}, {1216, 832}, {1344, 768}, {1536, 640},
                                  {640, 1536},  {768, 1344}, {832, 1216}, {896, 1152}};
  int best = 0, bd = 1 << 30;
  for (int i = 0; i < 9; ++i) {
    int d = std::abs(legal[i][0] - w) + std::abs(legal[i][1] - h);
    if (d < bd) { bd = d; best = i; }
  }
  w = legal[best][0];
  h = legal[best][1];
}

std::string stability_aspect(int w, int h) {
  static const char *names[] = {"1:1", "16:9", "21:9", "2:3", "3:2", "4:5", "5:4", "9:16", "9:21"};
  static const float vals[] = {1.f, 16.f / 9, 21.f / 9, 2.f / 3, 1.5f, 0.8f, 1.25f, 9.f / 16, 9.f / 21};
  float r = h > 0 ? (float)w / h : 1.f;
  int best = 0;
  for (int i = 1; i < 9; ++i)
    if (std::fabs(vals[i] - r) < std::fabs(vals[best] - r)) best = i;
  return names[best];
}

bool parse_stability_image(const std::string &body, std::string &png, unsigned &seed, std::string &err) {
  json j = json::parse(body, nullptr, false);
  if (j.is_discarded()) { err = "not JSON"; return false; }
  if (j.contains("errors") && j["errors"].is_array() && !j["errors"].empty()) {
    err = j["errors"][0].is_string() ? j["errors"][0].get<std::string>() : "error";
    return false;
  }
  if (j.contains("message") && !j.contains("image")) { err = j["message"].get<std::string>(); return false; }
  if (!j.contains("image") || !j["image"].is_string()) { err = "no image in the reply"; return false; }
  png = base64_decode(j["image"].get<std::string>());
  seed = j.value("seed", 0u);
  if (j.value("finish_reason", "SUCCESS") == "CONTENT_FILTERED") { err = "the provider filtered the content"; return false; }
  return !png.empty();
}

// ------------------------------------------------------------------ Gemini
std::string gemini_aspect(int w, int h) {
  static const char *names[] = {"1:1", "2:3", "3:2", "3:4", "4:3", "4:5", "5:4", "9:16", "16:9", "21:9"};
  static const float vals[] = {1.f, 2.f / 3, 1.5f, 0.75f, 4.f / 3, 0.8f, 1.25f, 9.f / 16, 16.f / 9, 21.f / 9};
  float r = h > 0 ? (float)w / h : 1.f;
  int best = 0;
  for (int i = 1; i < 10; ++i)
    if (std::fabs(vals[i] - r) < std::fabs(vals[best] - r)) best = i;
  return names[best];
}

bool parse_gemini_image(const std::string &body, std::string &bytes, std::string &mime, std::string &err) {
  json j = json::parse(body, nullptr, false);
  if (j.is_discarded()) { err = "not JSON"; return false; }
  if (j.contains("error")) { err = j["error"].value("message", "error"); return false; }
  std::string text;
  for (const json &c : j.value("candidates", json::array())) {
    for (const json &p : c.value("content", json::object()).value("parts", json::array())) {
      const json *inl = nullptr;
      if (p.contains("inlineData")) inl = &p["inlineData"];
      else if (p.contains("inline_data")) inl = &p["inline_data"];
      if (inl && inl->contains("data")) {
        bytes = base64_decode(inl->value("data", ""));
        mime = inl->value("mimeType", inl->value("mime_type", "image/png"));
        return !bytes.empty();
      }
      if (p.contains("text")) text += p.value("text", "");
    }
  }
  err = text.empty() ? "no image in the reply" : "no image; the model said: " + text;
  return false;
}

// -------------------------------------------------------------------- run
namespace {

bool write_bytes(const std::string &path, const std::string &bytes, std::string &err) {
  std::ofstream f(path, std::ios::binary);
  if (!f) { err = "cannot write " + path; return false; }
  f.write(bytes.data(), (std::streamsize)bytes.size());
  return true;
}

} // namespace

GenResult ai_generate_image(const GenImageRequest &req, std::atomic<bool> *cancel,
                            std::function<void(float, const std::string &)> progress) {
  GenResult res;
  std::string provider = req.provider.empty() ? config().ai.image_provider : req.provider;
  if (!service_ready(provider)) {
    res.message = provider + " is not configured (Settings > AI services)";
    return res;
  }
  ServiceConfig s = service_resolved(provider);
  std::string prompt = ai_prompt_for(req.kind, req.prompt);
  std::string negative = ai_negative_for(req.kind, req.negative);
  int w = req.width, h = req.height;
  ai_size_for(req.kind, w, h);
  if (progress) progress(0.05f, "asking " + provider);
  std::string bytes, err;

  if (provider == "comfyui") {
    std::string wf = comfy_find_workflow(req.workflow.empty() ? s.model : req.workflow, err);
    if (wf.empty()) { res.message = err; return res; }
    std::map<std::string, std::string> params = {
        {"prompt", prompt}, {"negative", negative}, {"width", std::to_string(w)},
        {"height", std::to_string(h)}};
    if (req.seed) params["seed"] = std::to_string(req.seed);
    if (req.steps) params["steps"] = std::to_string(req.steps);
    if (!comfy_inject(wf, comfy_infer_bindings(wf), params, err)) { res.message = err; return res; }
    if (!comfy_run(wf, req.out_path, cancel, progress, err)) { res.message = err; return res; }
    res.ok = true;
    res.path = req.out_path;
    res.seed = req.seed;
    return res;
  }
  if (provider == "openai_image") {
    HttpResponse r = http_post_json(s.endpoint + "/images/generations",
                                    openai_image_json(s.model, prompt, w, h),
                                    {{"Authorization", "Bearer " + s.key}});
    if (r.status == 0) { res.message = r.error; return res; }
    if (!parse_openai_image(r.body, bytes, err)) { res.message = err; return res; }
    if (bytes.rfind("url:", 0) == 0) {
      if (!http_download(bytes.substr(4), req.out_path, err)) { res.message = err; return res; }
      res.ok = true; res.path = req.out_path;
      return res;
    }
  } else if (provider == "stability") {
    std::string ct;
    std::vector<FormPart> parts = {{"prompt", prompt, "", ""},
                                   {"negative_prompt", negative, "", ""},
                                   {"aspect_ratio", stability_aspect(w, h), "", ""},
                                   {"output_format", "png", "", ""}};
    if (req.seed) parts.push_back({"seed", std::to_string(req.seed), "", ""});
    HttpRequest rq;
    rq.method = "POST";
    rq.url = s.endpoint + "/v2beta/stable-image/generate/" + (s.model.empty() ? "core" : s.model);
    rq.body = http_multipart(parts, ct);
    rq.headers = {{"Authorization", "Bearer " + s.key}, {"Accept", "application/json"}, {"Content-Type", ct}};
    HttpResponse r = http_send(rq);
    if (r.status == 0) { res.message = r.error; return res; }
    unsigned seed = 0;
    if (!parse_stability_image(r.body, bytes, seed, err)) { res.message = err; return res; }
    res.seed = seed;
  } else if (provider == "google_image") {
    std::string url = s.endpoint + "/models/" + s.model + ":generateContent?key=" + s.key;
    HttpResponse r = http_post_json(url, gemini_generate_json(prompt, "", true, gemini_aspect(w, h)));
    if (r.status == 0) { res.message = r.error; return res; }
    std::string mime;
    if (!parse_gemini_image(r.body, bytes, mime, err)) { res.message = err; return res; }
  } else {
    res.message = "no image adapter for " + provider;
    return res;
  }
  if (!write_bytes(req.out_path, bytes, err)) { res.message = err; return res; }
  if (progress) progress(1.f, "done");
  res.ok = true;
  res.path = req.out_path;
  return res;
}

} // namespace studio
