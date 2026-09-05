// Geekatplay TerraForge - AI generation and configuration as operations, so
// the assistant, the Python API and MCP can start a texture, a skydome or a
// 3D model, read the job list, ask a text model, and set a service key.
#include "ai_describe.hpp"
#include "ai_jobs.hpp"
#include "ai_services.hpp"
#include "app.hpp"
#include "config.hpp"
#include "material_ui.hpp"
#include <json.hpp>
#include <string>

using nlohmann::json;

namespace studio {

gpx::Node *find_node(App &a, const json &act, const char *key);

int ai_generate_op(App &a, const std::string &op, const json &act, std::string &err) {
  if (op == "ai_generate_image" || op == "ai_generate_texture" || op == "ai_generate_skydome") {
    std::string prompt = act.value("prompt", std::string());
    if (prompt.empty()) {
      err = op + " needs a 'prompt'";
      return 0;
    }
    auto job = std::make_shared<AiJob>();
    std::string kind = act.value("kind", op == "ai_generate_texture" ? "texture" : op == "ai_generate_skydome" ? "skydome" : "image");
    job->kind = kind == "texture" ? JOB_TEXTURE : kind == "skydome" ? JOB_SKYDOME : JOB_IMAGE;
    job->provider = act.value("provider", std::string());
    job->prompt = prompt;
    job->negative = act.value("negative", std::string());
    job->width = act.value("width", 1024);
    job->height = act.value("height", job->width);
    job->seed = act.value("seed", 0u);
    job->workflow = act.value("workflow", std::string());
    if (job->kind == JOB_TEXTURE && (act.contains("material") || act.value("apply", false))) {
      gpx::Node *m = act.contains("material") ? find_node(a, act, "material")
                                              : a.graph.find_node(material_studio().material);
      if (m) {
        job->apply.material = m->id;
        job->apply.channel = act.value("channel", std::string("base color"));
      }
    }
    if (job->kind == JOB_SKYDOME) job->apply.as_skydome = act.value("apply", true);
    uint64_t id = ai_job_submit(job);
    a.status = "AI job " + std::to_string(id) + " started: " + ai_job_kind_name(job->kind) + " via " +
               (job->provider.empty() ? config().ai.image_provider : job->provider);
    return 1;
  }

  if (op == "ai_generate_model") {
    auto job = std::make_shared<AiJob>();
    job->kind = JOB_MODEL;
    job->provider = act.value("provider", std::string());
    job->prompt = act.value("prompt", std::string());
    job->image_path = act.value("image", std::string());
    if (job->prompt.empty() && job->image_path.empty()) {
      err = "ai_generate_model needs a 'prompt' or an 'image'";
      return 0;
    }
    job->apply.import_object = act.value("import", true);
    uint64_t id = ai_job_submit(job);
    a.status = "AI job " + std::to_string(id) + " started: 3D model via " +
               (job->provider.empty() ? config().ai.model_provider : job->provider);
    return 1;
  }

  if (op == "ai_describe") {
    // the scene, the terrain or the atmosphere from words, as a job whose
    // answer is applied when it lands
    std::string prompt = act.value("prompt", std::string());
    if (prompt.empty()) { err = "ai_describe needs a 'prompt'"; return 0; }
    std::string scope = act.value("scope", std::string("scene"));
    int sc = scope == "terrain" ? DESCRIBE_TERRAIN : scope == "atmosphere" ? DESCRIBE_ATMOSPHERE : DESCRIBE_SCENE;
    uint64_t id = ai_describe_submit(prompt, act.value("image", std::string()), sc);
    a.status = "AI job " + std::to_string(id) + " started: describe the " + scope + " via " + config().ai.text_provider;
    return 1;
  }

  if (op == "ai_ask") {
    // a blocking question to the text model; the answer is the status line
    std::string prompt = act.value("prompt", std::string());
    if (prompt.empty()) { err = "ai_ask needs a 'prompt'"; return 0; }
    std::string out;
    if (!ai_text(act.value("provider", std::string()), act.value("system", std::string("You are a concise assistant inside a 3D terrain studio.")),
                 prompt, act.value("image", std::string()), out, err))
      return 0;
    a.status = out;
    return 1;
  }

  if (op == "ai_jobs") {
    std::string line;
    for (auto &j : ai_jobs()) {
      std::string msg, res;
      {
        std::lock_guard<std::mutex> lk(j->mtx);
        msg = j->message;
        res = j->result_path;
      }
      line += (line.empty() ? "" : "; ") + std::to_string(j->id) + " " + ai_job_kind_name(j->kind) + " " +
              ai_job_state_name(j->state.load()) + " " + std::to_string((int)(j->progress.load() * 100)) + "%" +
              (res.empty() ? (msg.empty() ? "" : " (" + msg + ")") : " -> " + res);
    }
    a.status = line.empty() ? "no AI jobs" : line;
    return 1;
  }

  if (op == "ai_job_cancel") {
    ai_job_cancel(act.value("id", 0ull));
    a.status = "cancel requested";
    return 1;
  }

  if (op == "config_set_service") {
    std::string id = act.value("service", std::string());
    if (!provider_info(id)) { err = "unknown service '" + id + "'"; return 0; }
    ServiceConfig &s = config().services[id];
    if (act.contains("key")) s.key = act["key"].get<std::string>();
    if (act.contains("endpoint")) s.endpoint = act["endpoint"].get<std::string>();
    if (act.contains("model")) s.model = act["model"].get<std::string>();
    if (act.contains("enabled")) s.enabled = act["enabled"].get<bool>();
    config_save();
    a.status = id + (service_ready(id) ? " is ready" : " is configured but not ready");
    return 1;
  }

  if (op == "config_set_defaults") {
    AiDefaults &d = config().ai;
    if (act.contains("text_provider")) d.text_provider = act["text_provider"].get<std::string>();
    if (act.contains("image_provider")) d.image_provider = act["image_provider"].get<std::string>();
    if (act.contains("model_provider")) d.model_provider = act["model_provider"].get<std::string>();
    if (act.contains("comfy_url")) config().comfy.url = act["comfy_url"].get<std::string>();
    if (act.contains("comfy_install")) config().comfy.install_path = act["comfy_install"].get<std::string>();
    config_save();
    a.status = "defaults: text " + d.text_provider + ", image " + d.image_provider + ", 3D " + d.model_provider;
    return 1;
  }

  if (op == "config_status") {
    std::string line;
    for (const ProviderInfo &p : known_providers())
      line += (line.empty() ? "" : ", ") + std::string(p.id) + (service_ready(p.id) ? " ready" : " off");
    a.status = line + "; comfy " + comfy_base_url();
    return 1;
  }

  if (op == "config_check_comfy") {
    ComfyServerInfo info;
    if (!comfy_probe(comfy_base_url(), info, err)) return 0;
    a.status = "ComfyUI at " + comfy_base_url() + ": " + std::to_string(info.node_types) + " node types, " +
               std::to_string(info.checkpoints) + " checkpoints";
    return 1;
  }

  return -1;
}

} // namespace studio
