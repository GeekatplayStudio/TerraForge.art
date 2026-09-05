// Geekatplay TerraForge - natural language for the whole scene: "a foggy
// fjord at dawn, steep granite walls, a lone pine on the near shore". The
// planner gets every action schema the studio has - terrain graph, world
// (sun, sky, clouds, fog, water), objects, cameras, materials, render, and
// the AI generators themselves - and answers with an ordered JSON list of
// actions, which run through the same ai_apply_actions path as the
// assistant bars, the Python API and MCP. The model runs on the job
// thread; the actions are applied on the UI thread when the answer lands.
#include "ai_describe.hpp"
#include "ai_assist.hpp"
#include "ai_jobs.hpp"
#include "app.hpp"
#include "config.hpp"
#include "console.hpp"
#include <cstring>
#include <imgui.h>
#include <mutex>

namespace studio {

std::string dialog_open_file(const char *filter, const char *def_ext);

std::string ai_describe_system_prompt(int scope) {
  std::string s =
      "You build scenes in Geekatplay TerraForge, a node-based 3D terrain and environment "
      "studio, from a natural-language description.\n"
      "Answer with ONLY a JSON object of the form {\"actions\":[ ... ]}, the actions in the "
      "order to apply them, no prose, no markdown fences. Use only the ops listed below. Prefer a few well-chosen actions over "
      "many. Set the terrain first (graph ops), then the world (sun, sky, clouds, fog, water), "
      "then objects and cameras, then materials and render settings.\n"
      "When the description asks for a texture, a sky picture or a 3D model that must be "
      "painted or modelled rather than set, use ai_generate_texture, ai_generate_skydome or "
      "ai_generate_model with a precise prompt.\n\n";
  if (scope == DESCRIBE_TERRAIN || scope == DESCRIBE_SCENE) s += ai_action_schema(AiDomain::Terrain) + "\n";
  if (scope == DESCRIBE_ATMOSPHERE || scope == DESCRIBE_SCENE) s += ai_action_schema(AiDomain::World) + "\n";
  if (scope == DESCRIBE_SCENE) {
    s += ai_action_schema(AiDomain::Object) + "\n";
    s += ai_action_schema(AiDomain::Camera) + "\n";
    s += ai_action_schema(AiDomain::Material) + "\n";
    s += ai_action_schema(AiDomain::Render) + "\n";
  }
  s += "Generation ops:\n"
       "- {\"op\":\"ai_generate_texture\",\"prompt\":\"...\",\"apply\":true}\n"
       "- {\"op\":\"ai_generate_skydome\",\"prompt\":\"...\",\"apply\":true}\n"
       "- {\"op\":\"ai_generate_model\",\"prompt\":\"...\",\"import\":true}\n";
  return s;
}

uint64_t ai_describe_submit(const std::string &prompt, const std::string &image, int scope) {
  auto job = std::make_shared<AiJob>();
  job->kind = JOB_TEXT;
  job->provider = config().ai.text_provider;
  job->negative = ai_describe_system_prompt(scope); // the system prompt rides here
  job->prompt = prompt;
  job->image_path = image;
  job->apply.channel = "describe"; // marks the answer as actions to apply
  return ai_job_submit(job);
}

bool ai_describe_apply(App &a, AiJob &job, std::string &err) {
  std::string text;
  {
    std::lock_guard<std::mutex> lk(job.mtx);
    text = job.text_result;
  }
  std::string doc = ai_describe_extract_actions(text);
  if (doc.empty()) {
    err = "the model did not answer with actions: " + text.substr(0, 200);
    log_error("ai", "describe: no actions in the reply: " + text.substr(0, 2000));
    return false;
  }
  bool ok = ai_apply_actions(a, doc, err);
  if (!ok) log_error("ai", "describe: " + err + " | reply: " + text.substr(0, 2000));
  else log_info("ai", "describe: applied " + std::to_string(doc.size()) + " bytes of actions");
  return ok;
}

// ---------------------------------------------------------------- window
namespace {
bool g_show = false;
int g_scope = DESCRIBE_SCENE;
char g_prompt[2048] = "";
char g_image[512] = "";
} // namespace

void ai_describe_open(int scope) {
  g_scope = scope;
  g_show = true;
}

void draw_panel_ai_describe(App &a) {
  if (!g_show) return;
  ImGui::SetNextWindowSize(ImVec2(560, 300), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Describe", &g_show)) { ImGui::End(); return; }
  const char *scopes[] = {"The whole scene", "The terrain", "The atmosphere"};
  ImGui::SetNextItemWidth(200);
  ImGui::Combo("Build", &g_scope, scopes, 3);
  ImGui::SameLine();
  ImGui::TextDisabled("with %s", provider_info(config().ai.text_provider) ? provider_info(config().ai.text_provider)->name : config().ai.text_provider.c_str());
  ImGui::InputTextMultiline("##desc", g_prompt, sizeof g_prompt, ImVec2(-1, 120));
  if (g_prompt[0] == 0) {
    ImGui::SameLine(12);
    ImGui::TextDisabled("a foggy fjord at dawn, steep granite walls, snow above the tree line, a lone pine on the near shore...");
  }
  ImGui::SetNextItemWidth(-90);
  ImGui::InputTextWithHint("##img", "reference picture (optional)", g_image, sizeof g_image);
  ImGui::SameLine();
  if (ImGui::Button("Browse...")) {
    std::string p = dialog_open_file("Images\0*.png;*.jpg;*.jpeg\0", "png");
    if (!p.empty()) snprintf(g_image, sizeof g_image, "%s", p.c_str());
  }
  ImGui::BeginDisabled(g_prompt[0] == 0 || !service_ready(config().ai.text_provider));
  if (ImGui::Button("Build it", ImVec2(140, 0))) {
    uint64_t id = ai_describe_submit(g_prompt, g_image, g_scope);
    a.status = "describing the scene to " + config().ai.text_provider + " (job " + std::to_string(id) + ")";
    ai_generate_open_jobs();
  }
  ImGui::EndDisabled();
  if (!service_ready(config().ai.text_provider)) {
    ImGui::SameLine();
    ImGui::TextDisabled("no text model is ready: Settings > AI services");
  }
  ImGui::End();
}

} // namespace studio
