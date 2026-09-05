// Geekatplay TerraForge - the configuration manager: one file for everything
// the application needs to know about the world outside the project. API
// keys and endpoints for AI services, the ComfyUI installation, paths to
// external applications, keyboard shortcuts, output folders.
//
// Preferences (prefs.hpp) are how the application looks and performs;
// configuration is what it connects to. Kept apart so a preferences reset
// never loses a key, and so the secrets can be protected on their own.
//
// The file is <LOCALAPPDATA>/GeekatplayTerraForge/config.json. Secrets are
// written protected (Windows DPAPI for the current user; elsewhere a marked
// plain value, said so in the file) and read back transparently.
#pragma once
#include <map>
#include <string>
#include <vector>

namespace studio {

// One AI or web service: where it is, how it is authorised, what to ask for.
struct ServiceConfig {
  std::string key;      // the API key or token; "ak:sk" for Hitem3D
  std::string endpoint; // base URL override; empty = the provider's default
  std::string model;    // default model for the service, where it applies
  bool enabled = true;
};

struct ComfyConfig {
  std::string url = "http://127.0.0.1:8188";
  std::string cloud_url = "https://cloud.comfy.org";
  std::string cloud_key;
  int mode = 0; // 0 auto (local, then cloud), 1 local, 2 cloud
  std::string install_path;             // the ComfyUI folder, for workflows and models
  std::vector<std::string> workflow_dirs; // more folders of API-format workflows
  int poll_ms = 1000;
  int timeout_s = 1800;
};

struct AiDefaults {
  std::string text_provider = "ollama";   // who answers natural language
  std::string image_provider = "comfyui"; // who paints textures and skies
  std::string model_provider = "meshy";   // who builds 3D models
  std::string textures_dir;  // generated textures land here (default: library/textures)
  std::string skies_dir;     // generated skydomes (default: library/skies)
  std::string models_dir;    // generated meshes (default: library/models)
  bool notify_on_finish = true;
};

// The performance governor (perf.hpp): thresholds under which the viewport
// is kept responsive by lightening what costs the most.
struct PerfConfig {
  bool governor = true;
  int fps_primary = 30;   // the main view must be able to run at this
  int fps_secondary = 20; // other views, the preview: lightened first
};

struct Config {
  std::map<std::string, ServiceConfig> services; // keyed by provider id
  PerfConfig perf;
  ComfyConfig comfy;
  AiDefaults ai;
  std::map<std::string, std::string> apps;      // external applications by name -> path
  std::map<std::string, std::string> shortcuts; // command id -> chord ("Ctrl+S")
  int version = 1;
};

// The provider ids the application knows, with a display name and what it
// is for, so the settings screen and the services can agree on the spelling.
struct ProviderInfo {
  const char *id;
  const char *name;
  const char *purpose;      // "text", "image", "3d", "upscale", "local"
  const char *default_endpoint;
  const char *default_model;
  const char *key_hint;     // where to get a key
};
const std::vector<ProviderInfo> &known_providers();
const ProviderInfo *provider_info(const std::string &id);

Config &config();
std::string config_file();
bool config_load();
bool config_save();

// A service's settings with the provider defaults filled in.
ServiceConfig service_resolved(const std::string &id);
std::string service_endpoint(const std::string &id);
std::string service_model(const std::string &id);
bool service_ready(const std::string &id); // enabled, and keyed when a key is needed

// Secrets on disk. Round-trips; the protected form is opaque text.
std::string secret_protect(const std::string &plain);
std::string secret_unprotect(const std::string &stored);

// Serialise / parse without touching disk (for tests and for export).
std::string config_to_json(const Config &c, bool protect_secrets);
bool config_from_json(Config &c, const std::string &text, std::string &err);

// Where generated assets go, folders created on demand.
std::string config_output_dir(const char *kind); // "textures", "skies", "models"

} // namespace studio
