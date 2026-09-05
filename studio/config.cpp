// Geekatplay TerraForge - the configuration manager. See config.hpp.
#include "config.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <sstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#endif

using nlohmann::json;
namespace fs = std::filesystem;

namespace studio {

namespace {

Config g_config;
bool g_loaded = false;

std::string settings_base() {
  const char *base = std::getenv("LOCALAPPDATA");
  fs::path dir = base ? fs::path(base) : fs::temp_directory_path();
  dir = dir / "GeekatplayTerraForge";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir.string();
}

const char *const B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string b64_encode(const std::string &in) {
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

std::string b64_decode(const std::string &in) {
  std::string out;
  std::vector<int> T(256, -1);
  for (int i = 0; i < 64; i++) T[(unsigned char)B64[i]] = i;
  int val = 0, bits = -8;
  for (unsigned char c : in) {
    if (T[c] == -1) break;
    val = (val << 6) + T[c];
    bits += 6;
    if (bits >= 0) {
      out.push_back(char((val >> bits) & 0xFF));
      bits -= 8;
    }
  }
  return out;
}

} // namespace

// ------------------------------------------------------------- providers
const std::vector<ProviderInfo> &known_providers() {
  static const std::vector<ProviderInfo> p = {
      {"ollama", "Ollama (local)", "text", "http://127.0.0.1:11434", "llama3.1", "runs locally, no key"},
      {"openai", "OpenAI", "text", "https://api.openai.com/v1", "gpt-4o-mini", "platform.openai.com/api-keys"},
      {"anthropic", "Anthropic", "text", "https://api.anthropic.com/v1", "claude-sonnet-5", "console.anthropic.com"},
      {"google", "Google Gemini", "text", "https://generativelanguage.googleapis.com/v1beta", "gemini-2.0-flash", "aistudio.google.com/apikey"},
      {"comfyui", "ComfyUI (local)", "image", "http://127.0.0.1:8188", "text_to_image", "runs locally, no key"},
      {"openai_image", "OpenAI Images", "image", "https://api.openai.com/v1", "gpt-image-1", "same key as OpenAI"},
      {"stability", "Stability AI", "image", "https://api.stability.ai", "core", "platform.stability.ai"},
      {"google_image", "Google Imagen", "image", "https://generativelanguage.googleapis.com/v1beta", "gemini-2.5-flash-image", "same key as Gemini"},
      {"replicate", "Replicate", "upscale", "https://api.replicate.com/v1", "", "replicate.com/account"},
      {"fal", "fal.ai", "upscale", "https://fal.run", "fal-ai/clarity-upscaler", "fal.ai/dashboard/keys"},
      {"meshy", "Meshy", "3d", "https://api.meshy.ai/openapi", "meshy-4", "app.meshy.ai/settings/api"},
      {"tripo", "Tripo", "3d", "https://api.tripo3d.ai/v2/openapi", "", "platform.tripo3d.ai"},
      {"hitem3d", "Hitem3D", "3d", "https://api.hitem3d.ai/open-api/v1", "hitem3dv1.5", "key as ak:sk or a bearer token"},
  };
  return p;
}

const ProviderInfo *provider_info(const std::string &id) {
  for (const ProviderInfo &p : known_providers())
    if (id == p.id) return &p;
  return nullptr;
}

// --------------------------------------------------------------- secrets
std::string secret_protect(const std::string &plain) {
  if (plain.empty()) return "";
#ifdef _WIN32
  DATA_BLOB in{(DWORD)plain.size(), (BYTE *)plain.data()}, out{};
  if (CryptProtectData(&in, L"TerraForge", nullptr, nullptr, nullptr, 0, &out)) {
    std::string enc((const char *)out.pbData, out.cbData);
    LocalFree(out.pbData);
    return "dpapi:" + b64_encode(enc);
  }
#endif
  // no user-bound protection on this platform: say so in the file rather
  // than pretend
  return "plain:" + b64_encode(plain);
}

std::string secret_unprotect(const std::string &stored) {
  if (stored.rfind("plain:", 0) == 0) return b64_decode(stored.substr(6));
#ifdef _WIN32
  if (stored.rfind("dpapi:", 0) == 0) {
    std::string enc = b64_decode(stored.substr(6));
    DATA_BLOB in{(DWORD)enc.size(), (BYTE *)enc.data()}, out{};
    if (CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) {
      std::string plain((const char *)out.pbData, out.cbData);
      LocalFree(out.pbData);
      return plain;
    }
    return "";
  }
#endif
  return stored; // a key typed straight into the file still works
}

// ------------------------------------------------------------------ json
std::string config_to_json(const Config &c, bool protect) {
  json j;
  j["format"] = "terraforge-config";
  j["version"] = c.version;
  json sv = json::object();
  for (const auto &kv : c.services)
    sv[kv.first] = {{"key", protect ? secret_protect(kv.second.key) : kv.second.key},
                    {"endpoint", kv.second.endpoint},
                    {"model", kv.second.model},
                    {"enabled", kv.second.enabled}};
  j["services"] = sv;
  j["comfy"] = {{"url", c.comfy.url},
                {"cloud_url", c.comfy.cloud_url},
                {"cloud_key", protect ? secret_protect(c.comfy.cloud_key) : c.comfy.cloud_key},
                {"mode", c.comfy.mode},
                {"install_path", c.comfy.install_path},
                {"workflow_dirs", c.comfy.workflow_dirs},
                {"poll_ms", c.comfy.poll_ms},
                {"timeout_s", c.comfy.timeout_s}};
  j["ai"] = {{"text_provider", c.ai.text_provider},
             {"image_provider", c.ai.image_provider},
             {"model_provider", c.ai.model_provider},
             {"textures_dir", c.ai.textures_dir},
             {"skies_dir", c.ai.skies_dir},
             {"models_dir", c.ai.models_dir},
             {"notify_on_finish", c.ai.notify_on_finish}};
  j["apps"] = c.apps;
  j["shortcuts"] = c.shortcuts;
  return j.dump(1);
}

bool config_from_json(Config &c, const std::string &text, std::string &err) {
  json j = json::parse(text, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    err = "config is not a JSON object";
    return false;
  }
  Config n;
  n.version = j.value("version", 1);
  json services = j.value("services", json::object());
  if (!services.is_object()) services = json::object();
  for (auto &kv : services.items()) {
    if (!kv.value().is_object()) continue;
    ServiceConfig s;
    s.key = secret_unprotect(kv.value().value("key", ""));
    s.endpoint = kv.value().value("endpoint", "");
    s.model = kv.value().value("model", "");
    s.enabled = kv.value().value("enabled", true);
    n.services[kv.key()] = s;
  }
  json cf = j.value("comfy", json::object());
  if (!cf.is_object()) cf = json::object();
  n.comfy.url = cf.value("url", n.comfy.url);
  n.comfy.cloud_url = cf.value("cloud_url", n.comfy.cloud_url);
  n.comfy.cloud_key = secret_unprotect(cf.value("cloud_key", ""));
  n.comfy.mode = cf.value("mode", 0);
  n.comfy.install_path = cf.value("install_path", "");
  for (const auto &d : cf.value("workflow_dirs", json::array()))
    if (d.is_string()) n.comfy.workflow_dirs.push_back(d.get<std::string>());
  n.comfy.poll_ms = cf.value("poll_ms", 1000);
  n.comfy.timeout_s = cf.value("timeout_s", 1800);
  json ai = j.value("ai", json::object());
  if (!ai.is_object()) ai = json::object();
  n.ai.text_provider = ai.value("text_provider", n.ai.text_provider);
  n.ai.image_provider = ai.value("image_provider", n.ai.image_provider);
  n.ai.model_provider = ai.value("model_provider", n.ai.model_provider);
  n.ai.textures_dir = ai.value("textures_dir", "");
  n.ai.skies_dir = ai.value("skies_dir", "");
  n.ai.models_dir = ai.value("models_dir", "");
  n.ai.notify_on_finish = ai.value("notify_on_finish", true);
  json apps = j.value("apps", json::object());
  if (apps.is_object())
    for (auto &kv : apps.items())
      if (kv.value().is_string()) n.apps[kv.key()] = kv.value().get<std::string>();
  json sc = j.value("shortcuts", json::object());
  if (sc.is_object())
    for (auto &kv : sc.items())
      if (kv.value().is_string()) n.shortcuts[kv.key()] = kv.value().get<std::string>();
  c = n;
  return true;
}

// ------------------------------------------------------------------ disk
std::string config_file() { return (fs::path(settings_base()) / "config.json").string(); }

Config &config() {
  if (!g_loaded) config_load();
  return g_config;
}

bool config_load() {
  g_loaded = true;
  std::ifstream f(config_file(), std::ios::binary);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  std::string err;
  return config_from_json(g_config, ss.str(), err);
}

bool config_save() {
  std::ofstream f(config_file(), std::ios::binary);
  if (!f) return false;
  f << config_to_json(g_config, true);
  return (bool)f;
}

// -------------------------------------------------------------- services
ServiceConfig service_resolved(const std::string &id) {
  ServiceConfig s;
  auto it = config().services.find(id);
  if (it != config().services.end()) s = it->second;
  if (const ProviderInfo *p = provider_info(id)) {
    if (s.endpoint.empty()) s.endpoint = p->default_endpoint;
    if (s.model.empty()) s.model = p->default_model;
  }
  // the image and text twins share one key
  if (s.key.empty()) {
    if (id == "openai_image") s.key = service_resolved("openai").key;
    if (id == "google_image") s.key = service_resolved("google").key;
  }
  return s;
}

std::string service_endpoint(const std::string &id) { return service_resolved(id).endpoint; }
std::string service_model(const std::string &id) { return service_resolved(id).model; }

bool service_ready(const std::string &id) {
  ServiceConfig s = service_resolved(id);
  if (!s.enabled) return false;
  if (id == "ollama" || id == "comfyui") return true;
  return !s.key.empty();
}

std::string config_output_dir(const char *kind) {
  const AiDefaults &ai = config().ai;
  std::string custom = std::string(kind) == "textures" ? ai.textures_dir
                       : std::string(kind) == "skies"  ? ai.skies_dir
                                                        : ai.models_dir;
  fs::path dir = custom.empty() ? fs::path(settings_base()) / "library" / kind : fs::path(custom);
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir.string();
}

} // namespace studio
