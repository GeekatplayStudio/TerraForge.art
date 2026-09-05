// Geekatplay TerraForge - the AI services: natural language, images and
// textures, 360 skydomes, 3D models, from a local ComfyUI or Ollama or from
// the cloud providers configured in Settings.
//
// Design, ported from Image Express (docs_private/IMAGEEXPRESS_PORT.md):
// every provider is a thin adapter over its REST API with the exact wire
// format that provider documents; the request builders and response
// parsers are pure functions of strings, so they are tested on canned JSON
// without a network; results are written to disk before anyone is told,
// because provider URLs expire; async providers are polled with backoff.
//
// Everything here blocks. Callers are the job thread (ai_jobs.cpp) and the
// tests, never the UI thread.
#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace studio {

// ------------------------------------------------------------ natural language
// Ask the configured text model (config().ai.text_provider) or a named one.
// `image_path` attaches a picture for vision-capable models. Blocking.
bool ai_text(const std::string &provider, const std::string &system, const std::string &prompt,
             const std::string &image_path, std::string &out, std::string &err);

// Pure request builders / parsers (tested)
std::string openai_chat_json(const std::string &model, const std::string &system,
                             const std::string &prompt, const std::string &image_b64_png);
std::string anthropic_messages_json(const std::string &model, const std::string &system,
                                    const std::string &prompt, const std::string &image_b64_png);
std::string gemini_generate_json(const std::string &prompt, const std::string &image_b64_png,
                                 bool want_image, const std::string &aspect);
bool parse_openai_chat(const std::string &body, std::string &text, std::string &err);
bool parse_anthropic_message(const std::string &body, std::string &text, std::string &err);
bool parse_gemini_text(const std::string &body, std::string &text, std::string &err);

// ---------------------------------------------------------------------- images
enum ImageKind { IMG_PLAIN = 0, IMG_TEXTURE, IMG_SKYDOME };

struct GenImageRequest {
  std::string provider;   // openai_image, stability, google_image, comfyui
  std::string prompt;
  std::string negative;
  int kind = IMG_PLAIN;
  int width = 1024, height = 1024;
  unsigned seed = 0;      // 0 = random
  int steps = 0;          // 0 = provider default
  std::string workflow;   // comfyui: a workflow file or bundled name
  std::string out_path;   // where the PNG goes
};
struct GenResult {
  bool ok = false;
  std::string path;       // the file written
  std::string message;    // error or note
  unsigned seed = 0;
};

// The prompt a provider actually receives: the user's words wrapped in what
// a tileable texture or an equirectangular sky needs (ai_prompts.cpp).
std::string ai_prompt_for(int kind, const std::string &user_prompt);
std::string ai_negative_for(int kind, const std::string &user_negative);
// A 2:1 skydome size for a provider that wants a fixed aspect.
void ai_size_for(int kind, int &w, int &h);

GenResult ai_generate_image(const GenImageRequest &req, std::atomic<bool> *cancel,
                            std::function<void(float, const std::string &)> progress);

// Pure helpers (tested)
std::string openai_image_json(const std::string &model, const std::string &prompt, int w, int h);
std::string openai_size_for(int w, int h, const std::string &model);
bool parse_openai_image(const std::string &body, std::string &png_bytes, std::string &err);
void stability_snap_size(int &w, int &h);
std::string stability_aspect(int w, int h);
bool parse_stability_image(const std::string &body, std::string &png_bytes, unsigned &seed,
                           std::string &err);
std::string gemini_aspect(int w, int h);
bool parse_gemini_image(const std::string &body, std::string &bytes, std::string &mime,
                        std::string &err);

// --------------------------------------------------------------------- ComfyUI
struct ComfyServerInfo {
  int node_types = 0;
  int checkpoints = 0;
  std::vector<std::string> checkpoint_names;
  std::string version;
};
std::string comfy_base_url();      // per config().comfy.mode
std::map<std::string, std::string> comfy_headers();
bool comfy_probe(const std::string &base, ComfyServerInfo &info, std::string &err);

// A binding: which node input a parameter lands in.
struct ComfyBinding {
  std::string source;    // prompt, negative, seed, steps, cfg, width, height, image, strength
  std::string node_id;
  std::string input;
};
// Inject by (node id, input name); empty values are skipped so the
// workflow's own defaults survive; a missing node is an error.
bool comfy_inject(std::string &workflow_json, const std::vector<ComfyBinding> &bindings,
                  const std::map<std::string, std::string> &params, std::string &err);
// Guess the bindings of an API-format workflow from its node classes and
// input names, the way Image Express does for imported workflows.
std::vector<ComfyBinding> comfy_infer_bindings(const std::string &workflow_json);
std::vector<std::string> comfy_output_nodes(const std::string &workflow_json);
// The bundled SDXL text-to-image workflow (API format).
std::string comfy_bundled_workflow(const std::string &name);
// Find a workflow by name in the configured folders, else bundled.
std::string comfy_find_workflow(const std::string &name, std::string &err);
// Run: queue, poll history, download the first image to out_path.
bool comfy_run(const std::string &workflow_json, const std::string &out_path,
               std::atomic<bool> *cancel,
               std::function<void(float, const std::string &)> progress, std::string &err);
bool parse_comfy_history(const std::string &body, const std::string &prompt_id,
                         std::string &filename, std::string &subfolder, std::string &type);

// ------------------------------------------------------------------- 3D models
struct GenModelRequest {
  std::string provider;   // meshy, tripo, hitem3d
  std::string prompt;     // text-to-3D
  std::string image_path; // image-to-3D (either or both)
  std::string out_dir;
  bool pbr = true;
};
GenResult ai_generate_model(const GenModelRequest &req, std::atomic<bool> *cancel,
                            std::function<void(float, const std::string &)> progress);

// Pure helpers (tested): a provider's task status as one shape
struct TaskStatus {
  enum State { PENDING, RUNNING, SUCCEEDED, FAILED } state = PENDING;
  float progress = 0.f;
  std::string result_url;
  std::string thumb_url;
  std::string message;
  std::string task_id;
};
bool parse_meshy_status(const std::string &body, TaskStatus &st);
bool parse_tripo_status(const std::string &body, TaskStatus &st);
bool parse_hitems_status(const std::string &body, TaskStatus &st);
std::string hitems_auth_header(const std::string &key); // "Basic ..." or "Bearer ..."
std::string meshy_text_json(const std::string &prompt, const std::string &model);
std::string tripo_text_json(const std::string &prompt);
// Polling backoff (Image Express): min 2 s, x1.5 to 15 s, reset on progress.
int poll_next_delay_ms(int current_ms, bool progressed);

} // namespace studio
