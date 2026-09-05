// Geekatplay TerraForge - the Generate windows: an image, a tileable
// texture or a 360 skydome from a prompt; a 3D model from a prompt or a
// picture. Pick the provider (any that is ready in Settings), write what you
// want, press Generate; the job runs in the background and the Jobs list
// shows it arriving. A finished result can go straight where it is for -
// onto a material's channel, into the sky, into the scene as an object.
#include "ai_jobs.hpp"
#include "ai_services.hpp"
#include "app.hpp"
#include "asset_store.hpp"
#include "config.hpp"
#include "material_ui.hpp"
#include <cstring>
#include <imgui.h>
#include <mutex>
#include <string>

namespace studio {

std::string dialog_open_file(const char *filter, const char *def_ext);

namespace {

bool g_show_image = false, g_show_model = false, g_show_jobs = false;
int g_kind = JOB_TEXTURE;
char g_prompt[1024] = "";
char g_negative[512] = "";
char g_workflow[256] = "";
char g_ref_image[512] = "";
char g_model_prompt[1024] = "";
int g_size = 1; // 0 512, 1 1024, 2 2048
int g_seed = 0;
bool g_apply_material = true, g_apply_sky = true, g_import_model = true;
std::string g_image_provider, g_model_provider;

void provider_combo(const char *label, std::string &value, const char *purpose_a, const char *purpose_b) {
  if (value.empty()) value = std::string(purpose_a) == "image" ? config().ai.image_provider : config().ai.model_provider;
  const ProviderInfo *cur = provider_info(value);
  ImGui::SetNextItemWidth(220);
  if (ImGui::BeginCombo(label, cur ? cur->name : value.c_str())) {
    for (const ProviderInfo &p : known_providers()) {
      if (std::string(p.purpose) != purpose_a && std::string(p.purpose) != purpose_b) continue;
      bool ready = service_ready(p.id);
      if (!ready) ImGui::BeginDisabled();
      if (ImGui::Selectable(p.name, value == p.id)) value = p.id;
      if (!ready) ImGui::EndDisabled();
      if (!ready && ImGui::IsItemHovered()) ImGui::SetTooltip("Add its key in Settings > AI services");
    }
    ImGui::EndCombo();
  }
}

void image_window(App &a) {
  if (!g_show_image) return;
  ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Generate image", &g_show_image)) { ImGui::End(); return; }
  const char *kinds[] = {"Image", "Tileable texture", "360 skydome"};
  int k = g_kind == JOB_TEXTURE ? 1 : g_kind == JOB_SKYDOME ? 2 : 0;
  ImGui::SetNextItemWidth(220);
  if (ImGui::Combo("What", &k, kinds, 3)) g_kind = k == 1 ? JOB_TEXTURE : k == 2 ? JOB_SKYDOME : JOB_IMAGE;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("A texture is asked for as a seamless top-down tile; a skydome as a 2:1 equirectangular panorama.");
  provider_combo("Provider", g_image_provider, "image", "image");
  ImGui::InputTextMultiline("##prompt", g_prompt, sizeof g_prompt, ImVec2(-1, 70));
  if (g_prompt[0] == 0) {
    ImGui::SameLine(12);
    ImGui::TextDisabled("describe it: wet mossy granite, fine cracks...");
  }
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##neg", "avoid (optional)", g_negative, sizeof g_negative);
  const char *sizes[] = {"512", "1024", "2048"};
  ImGui::SetNextItemWidth(100);
  ImGui::Combo("Size", &g_size, sizes, 3);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120);
  ImGui::InputInt("Seed (0 random)", &g_seed);
  if (g_image_provider == "comfyui") {
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##wf", "workflow (bundled text_to_image, or a name in your ComfyUI workflows folder)",
                             g_workflow, sizeof g_workflow);
  }
  ImGui::Separator();
  if (g_kind == JOB_TEXTURE) {
    studio::Checkbox("Use on the material open in the studio", &g_apply_material);
    gpx::Node *m = a.graph.find_node(material_studio().material);
    if (m) { ImGui::SameLine(); ImGui::TextDisabled("(%s)", m->attrs.get_s("name").c_str()); }
  } else if (g_kind == JOB_SKYDOME) {
    studio::Checkbox("Set as the sky when done", &g_apply_sky);
  }
  ImGui::BeginDisabled(g_prompt[0] == 0);
  if (ImGui::Button("Generate", ImVec2(140, 0))) {
    auto job = std::make_shared<AiJob>();
    job->kind = g_kind;
    job->provider = g_image_provider;
    job->prompt = g_prompt;
    job->negative = g_negative;
    job->width = job->height = g_size == 0 ? 512 : g_size == 1 ? 1024 : 2048;
    job->seed = (unsigned)std::max(g_seed, 0);
    job->workflow = g_workflow;
    if (g_kind == JOB_TEXTURE && g_apply_material) {
      job->apply.material = material_studio().material;
      job->apply.channel = "base color";
    }
    if (g_kind == JOB_SKYDOME) job->apply.as_skydome = g_apply_sky;
    ai_job_submit(job);
    a.status = std::string("generating a ") + ai_job_kind_name(g_kind) + " with " + g_image_provider;
    g_show_jobs = true;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Jobs...")) g_show_jobs = true;
  ImGui::End();
}

void model_window(App &a) {
  if (!g_show_model) return;
  ImGui::SetNextWindowSize(ImVec2(520, 320), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Generate 3D model", &g_show_model)) { ImGui::End(); return; }
  provider_combo("Provider", g_model_provider, "3d", "3d");
  ImGui::InputTextMultiline("##mprompt", g_model_prompt, sizeof g_model_prompt, ImVec2(-1, 60));
  if (g_model_prompt[0] == 0) { ImGui::SameLine(12); ImGui::TextDisabled("a weathered granite boulder..."); }
  ImGui::SetNextItemWidth(-90);
  ImGui::InputTextWithHint("##ref", "reference picture (image-to-3D, optional; Hitem3D needs one)", g_ref_image, sizeof g_ref_image);
  ImGui::SameLine();
  if (ImGui::Button("Browse...")) {
    std::string p = dialog_open_file("Images\0*.png;*.jpg;*.jpeg\0", "png");
    if (!p.empty()) snprintf(g_ref_image, sizeof g_ref_image, "%s", p.c_str());
  }
  studio::Checkbox("Import into the scene when done", &g_import_model);
  ImGui::BeginDisabled(g_model_prompt[0] == 0 && g_ref_image[0] == 0);
  if (ImGui::Button("Generate", ImVec2(140, 0))) {
    auto job = std::make_shared<AiJob>();
    job->kind = JOB_MODEL;
    job->provider = g_model_provider;
    job->prompt = g_model_prompt;
    job->image_path = g_ref_image;
    job->apply.import_object = g_import_model;
    ai_job_submit(job);
    a.status = "generating a 3D model with " + g_model_provider;
    g_show_jobs = true;
  }
  ImGui::EndDisabled();
  ImGui::TextDisabled("Results land in %s and in the asset index.", config_output_dir("models").c_str());
  ImGui::End();
}

void jobs_window(App &a) {
  if (!g_show_jobs) return;
  ImGui::SetNextWindowSize(ImVec2(620, 300), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("AI jobs", &g_show_jobs)) { ImGui::End(); return; }
  if (ImGui::SmallButton("Clear finished")) ai_jobs_clear_finished();
  if (ImGui::BeginTable("##jobs", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("", 0, 0.5f);
    ImGui::TableSetupColumn("Job", 0, 2.2f);
    ImGui::TableSetupColumn("State", 0, 0.8f);
    ImGui::TableSetupColumn("Progress", 0, 1.2f);
    ImGui::TableSetupColumn("", 0, 0.9f);
    ImGui::TableHeadersRow();
    for (auto &j : ai_jobs()) {
      ImGui::PushID((int)j->id);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      int s = j->state.load();
      std::string result, msg;
      {
        std::lock_guard<std::mutex> lk(j->mtx);
        result = j->result_path;
        msg = j->message;
      }
      if (s == JOB_DONE && !j->thumb_tex && !result.empty() && j->kind != JOB_MODEL && j->kind != JOB_TEXT) {
        gpx::AssetRecord r;
        r.kind = "texture";
        r.path = result;
        j->thumb_tex = asset_thumb_texture(r);
      }
      if (j->thumb_tex) ImGui::Image((ImTextureID)(intptr_t)j->thumb_tex, ImVec2(40, 40), ImVec2(0, 0), ImVec2(1, 1));
      else ImGui::TextDisabled("%s", ai_job_kind_name(j->kind));
      ImGui::TableNextColumn();
      ImGui::TextWrapped("%s: %s", ai_job_kind_name(j->kind), j->prompt.substr(0, 80).c_str());
      ImGui::TextDisabled("%s", j->provider.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(ai_job_state_name(s));
      ImGui::TableNextColumn();
      if (s == JOB_RUNNING) ImGui::ProgressBar(j->progress.load(), ImVec2(-1, 0));
      ImGui::TextWrapped("%s", s == JOB_DONE ? result.c_str() : msg.c_str());
      ImGui::TableNextColumn();
      if (s == JOB_RUNNING || s == JOB_QUEUED) {
        if (ImGui::SmallButton("Cancel")) ai_job_cancel(j->id);
      } else if (s == JOB_DONE && !result.empty()) {
        if (j->kind == JOB_MODEL) {
          if (ImGui::SmallButton("Import")) { j->apply.import_object = true; j->serviced = false; }
        } else if (j->kind == JOB_SKYDOME) {
          if (ImGui::SmallButton("Use as sky")) { j->apply.as_skydome = true; j->serviced = false; }
        } else if (j->kind != JOB_TEXT) {
          if (ImGui::SmallButton("Use on material")) {
            j->apply.material = material_studio().material;
            j->apply.channel = "base color";
            j->kind = JOB_TEXTURE;
            j->serviced = false;
          }
        }
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::End();
}

} // namespace

void ai_generate_open_image(int kind) { g_kind = kind; g_show_image = true; }
void ai_generate_open_model() { g_show_model = true; }
void ai_generate_open_jobs() { g_show_jobs = true; }

void ai_tool_buttons() {
  if (ImGui::SmallButton("AI model")) g_show_model = true;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Generate a 3D model from a prompt or a picture (Meshy, Tripo, Hitem3D).");
  ImGui::SameLine();
  if (ImGui::SmallButton("AI image")) { g_kind = JOB_IMAGE; g_show_image = true; }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Generate an image, a tileable texture or a 360 skydome.");
}

void draw_panel_ai_generate(App &a) {
  image_window(a);
  model_window(a);
  jobs_window(a);
}

} // namespace studio
