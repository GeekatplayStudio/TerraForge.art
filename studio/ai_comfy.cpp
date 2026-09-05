// Geekatplay TerraForge - the ComfyUI client: a local (or cloud) server that
// runs API-format workflows. Queue at /prompt, poll /history until the
// output node has an image, fetch it from /view. Parameters are injected by
// (node id, input name), inferred from the workflow's node classes when a
// workflow has no manifest - the Image Express way, so the same imported
// workflow behaves the same here.
#include "ai_services.hpp"
#include "config.hpp"
#include "http_client.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <random>
#include <sstream>
#include <thread>

using nlohmann::json;
namespace fs = std::filesystem;

namespace studio {

std::string comfy_base_url() {
  const ComfyConfig &c = config().comfy;
  if (c.mode == 2) return c.cloud_url;
  return c.url;
}

std::map<std::string, std::string> comfy_headers() {
  const ComfyConfig &c = config().comfy;
  std::map<std::string, std::string> h;
  if (c.mode == 2 && !c.cloud_key.empty()) h["X-API-Key"] = c.cloud_key;
  return h;
}

namespace {
std::string api_prefix() { return config().comfy.mode == 2 ? "/api" : ""; }
std::string history_prefix() { return config().comfy.mode == 2 ? "/api/history_v2" : "/history"; }

std::string lower(std::string s) {
  for (char &c : s) c = (char)tolower((unsigned char)c);
  return s;
}
bool has(const std::string &s, const char *sub) { return s.find(sub) != std::string::npos; }
} // namespace

bool comfy_probe(const std::string &base, ComfyServerInfo &info, std::string &err) {
  HttpRequest rq;
  rq.url = base + api_prefix() + "/object_info";
  rq.headers = comfy_headers();
  rq.connect_ms = 3000;
  rq.receive_ms = 20000;
  HttpResponse r = http_send(rq);
  if (!r.ok()) {
    err = r.error.empty() ? "no answer" : r.error;
    return false;
  }
  json j = json::parse(r.body, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    err = "the server did not answer like ComfyUI (is this its address?)";
    return false;
  }
  info.node_types = (int)j.size();
  // checkpoints: the loader's enum of files
  if (j.contains("CheckpointLoaderSimple")) {
    const json &in = j["CheckpointLoaderSimple"].value("input", json::object()).value("required", json::object());
    if (in.contains("ckpt_name") && in["ckpt_name"].is_array() && !in["ckpt_name"].empty() &&
        in["ckpt_name"][0].is_array())
      for (const json &n : in["ckpt_name"][0])
        if (n.is_string()) info.checkpoint_names.push_back(n.get<std::string>());
  }
  info.checkpoints = (int)info.checkpoint_names.size();
  return true;
}

// ---------------------------------------------------------------- inject
bool comfy_inject(std::string &wf_text, const std::vector<ComfyBinding> &bindings,
                  const std::map<std::string, std::string> &params, std::string &err) {
  json wf = json::parse(wf_text, nullptr, false);
  if (wf.is_discarded() || !wf.is_object()) {
    err = "the workflow is not an API-format JSON object";
    return false;
  }
  for (const ComfyBinding &b : bindings) {
    auto it = params.find(b.source);
    if (it == params.end() || it->second.empty()) continue; // keep the workflow's default
    if (!wf.contains(b.node_id)) {
      err = "the workflow has no node \"" + b.node_id + "\"";
      return false;
    }
    json &inputs = wf[b.node_id]["inputs"];
    const std::string &v = it->second;
    // numbers stay numbers: a seed or a width written as a string would be
    // refused by the server
    if (b.source == "seed" || b.source == "steps" || b.source == "width" || b.source == "height")
      inputs[b.input] = std::stoll(v);
    else if (b.source == "cfg" || b.source == "strength")
      inputs[b.input] = std::stod(v);
    else
      inputs[b.input] = v;
  }
  wf_text = wf.dump();
  return true;
}

std::vector<ComfyBinding> comfy_infer_bindings(const std::string &wf_text) {
  std::vector<ComfyBinding> out;
  json wf = json::parse(wf_text, nullptr, false);
  if (wf.is_discarded() || !wf.is_object()) return out;
  bool got_size = false, got_prompt = false, got_negative = false;
  for (auto &kv : wf.items()) {
    const json &n = kv.value();
    if (!n.is_object()) continue;
    std::string cls = lower(n.value("class_type", ""));
    std::string title = lower(n.value("_meta", json::object()).value("title", ""));
    const json &in = n.value("inputs", json::object());
    auto has_in = [&](const char *k) { return in.contains(k) && !in[k].is_array(); };
    if (has(cls, "textencode") || has(cls, "encode") && has_in("text")) {
      const char *keys[] = {"text", "prompt", "positive", "t5xxl", "clip_l"};
      for (const char *k : keys)
        if (has_in(k)) {
          bool neg = has(title, "negative");
          if (neg && !got_negative) { out.push_back({"negative", kv.key(), k}); got_negative = true; }
          else if (!neg && !got_prompt) { out.push_back({"prompt", kv.key(), k}); got_prompt = true; }
          break;
        }
      if (has_in("negative")) out.push_back({"negative", kv.key(), "negative"});
    }
    if ((has(cls, "loadimagemask") || has(cls, "mask")) && has_in("image")) out.push_back({"mask", kv.key(), "image"});
    else if (has(cls, "loadimage") && has_in("image")) out.push_back({"image", kv.key(), "image"});
    if (has(cls, "sampler") || has(cls, "scheduler")) {
      if (has_in("seed")) out.push_back({"seed", kv.key(), "seed"});
      if (has_in("noise_seed")) out.push_back({"seed", kv.key(), "noise_seed"});
      if (has_in("steps")) out.push_back({"steps", kv.key(), "steps"});
      if (has_in("cfg")) out.push_back({"cfg", kv.key(), "cfg"});
      if (has_in("guidance")) out.push_back({"cfg", kv.key(), "guidance"});
      if (has_in("denoise")) out.push_back({"strength", kv.key(), "denoise"});
    }
    if (!got_size && has_in("width") && has_in("height") &&
        (has(cls, "empty") || has(cls, "latent") || has(cls, "scale") || has(cls, "resize") ||
         has(cls, "image") || has(cls, "resolution"))) {
      out.push_back({"width", kv.key(), "width"});
      out.push_back({"height", kv.key(), "height"});
      got_size = true;
    }
  }
  return out;
}

std::vector<std::string> comfy_output_nodes(const std::string &wf_text) {
  std::vector<std::string> out;
  json wf = json::parse(wf_text, nullptr, false);
  if (wf.is_discarded() || !wf.is_object()) return out;
  for (auto &kv : wf.items()) {
    std::string cls = lower(kv.value().value("class_type", ""));
    if ((has(cls, "save") || has(cls, "preview")) && has(cls, "image")) out.push_back(kv.key());
  }
  return out;
}

std::string comfy_bundled_workflow(const std::string &name) {
  if (name == "text_to_image" || name.empty() || name == "sdxl")
    return R"({"3":{"class_type":"KSampler","inputs":{"seed":123456789,"steps":28,"cfg":6.5,"sampler_name":"euler","scheduler":"normal","denoise":1,"model":["4",0],"positive":["6",0],"negative":["7",0],"latent_image":["5",0]}},
"4":{"class_type":"CheckpointLoaderSimple","inputs":{"ckpt_name":"sd_xl_base_1.0.safetensors"}},
"5":{"class_type":"EmptyLatentImage","inputs":{"width":1024,"height":1024,"batch_size":1}},
"6":{"class_type":"CLIPTextEncode","inputs":{"text":"a seamless rock texture","clip":["4",1]},"_meta":{"title":"positive"}},
"7":{"class_type":"CLIPTextEncode","inputs":{"text":"low quality, blurry, watermark","clip":["4",1]},"_meta":{"title":"negative"}},
"8":{"class_type":"VAEDecode","inputs":{"samples":["3",0],"vae":["4",2]}},
"9":{"class_type":"SaveImage","inputs":{"filename_prefix":"TerraForge","images":["8",0]}}})";
  return "";
}

std::string comfy_find_workflow(const std::string &name, std::string &err) {
  // a path, a file in a configured folder, or a bundled name
  std::error_code ec;
  std::vector<fs::path> dirs;
  const ComfyConfig &c = config().comfy;
  if (!c.install_path.empty()) {
    dirs.push_back(fs::path(c.install_path) / "user" / "default" / "workflows");
    dirs.push_back(fs::path(c.install_path) / "ComfyUI" / "user" / "default" / "workflows");
  }
  for (const std::string &d : c.workflow_dirs) dirs.push_back(d);
  std::vector<fs::path> candidates = {fs::path(name)};
  for (const fs::path &d : dirs) {
    candidates.push_back(d / name);
    candidates.push_back(d / (name + ".json"));
  }
  for (const fs::path &p : candidates)
    if (fs::is_regular_file(p, ec)) {
      std::ifstream f(p, std::ios::binary);
      std::stringstream ss;
      ss << f.rdbuf();
      return ss.str();
    }
  std::string b = comfy_bundled_workflow(name);
  if (b.empty()) err = "no workflow called '" + name + "' (bundled: text_to_image)";
  return b;
}

bool parse_comfy_history(const std::string &body, const std::string &prompt_id, std::string &filename,
                         std::string &subfolder, std::string &type) {
  json j = json::parse(body, nullptr, false);
  if (j.is_discarded() || !j.is_object() || !j.contains(prompt_id)) return false;
  const json &outputs = j[prompt_id].value("outputs", json::object());
  for (auto &kv : outputs.items()) {
    const json &imgs = kv.value().value("images", json::array());
    if (imgs.is_array() && !imgs.empty()) {
      filename = imgs[0].value("filename", "");
      subfolder = imgs[0].value("subfolder", "");
      type = imgs[0].value("type", "output");
      return !filename.empty();
    }
  }
  return false;
}

bool comfy_run(const std::string &wf, const std::string &out_path, std::atomic<bool> *cancel,
               std::function<void(float, const std::string &)> progress, std::string &err) {
  const std::string base = comfy_base_url();
  std::mt19937 rng((unsigned)std::chrono::steady_clock::now().time_since_epoch().count());
  char cid[40];
  snprintf(cid, sizeof cid, "terraforge-%08x%08x", rng(), rng());
  json body = {{"prompt", json::parse(wf)}, {"client_id", cid}};
  if (progress) progress(0.1f, "queueing at " + base);
  HttpResponse r = http_post_json(base + api_prefix() + "/prompt", body.dump(), comfy_headers());
  if (!r.ok()) {
    err = r.status ? "ComfyUI refused the workflow: " + r.body.substr(0, 300) : r.error;
    return false;
  }
  json q = json::parse(r.body, nullptr, false);
  std::string pid = q.is_object() ? q.value("prompt_id", "") : "";
  if (pid.empty()) {
    err = "no prompt_id in the answer";
    return false;
  }
  const int poll = std::max(config().comfy.poll_ms, 250);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(config().comfy.timeout_s);
  std::string filename, subfolder, type;
  int ticks = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    if (cancel && cancel->load()) { err = "cancelled"; return false; }
    std::this_thread::sleep_for(std::chrono::milliseconds(poll));
    HttpResponse h = http_get(base + history_prefix() + "/" + pid, comfy_headers());
    if (h.ok() && parse_comfy_history(h.body, pid, filename, subfolder, type)) break;
    ++ticks;
    if (progress) progress(0.1f + 0.8f * (1.f - 1.f / (1.f + ticks * 0.05f)), "rendering...");
  }
  if (filename.empty()) {
    err = "ComfyUI did not finish in time";
    return false;
  }
  if (progress) progress(0.95f, "fetching the image");
  std::string url = base + api_prefix() + "/view?filename=" + filename + "&subfolder=" + subfolder +
                    "&type=" + type;
  return http_download(url, out_path, err, comfy_headers());
}

} // namespace studio
