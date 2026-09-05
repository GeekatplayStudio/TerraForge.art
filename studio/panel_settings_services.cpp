// Geekatplay TerraForge - the Settings tabs for AI services and ComfyUI.
//
// Every known provider gets a row: on/off, its key (hidden, with a reveal
// toggle), the endpoint to use instead of the provider's own, and a model.
// The defaults - who answers natural language, who paints, who builds 3D -
// are chosen from the providers that are ready. ComfyUI has its own tab:
// local and cloud addresses, the mode, the installation folder and extra
// workflow folders, and a "Check" that asks the server what it is.
#include "ai_services.hpp"
#include "app.hpp"
#include "config.hpp"
#include <cstring>
#include <imgui.h>
#include <map>
#include <string>

namespace studio {

std::string dialog_open_file(const char *filter, const char *def_ext);

namespace {

std::map<std::string, bool> g_reveal;
std::string g_check_result;
char g_wf_dir[512] = "";

void text_field(const char *label, std::string &value, float width, bool secret,
                const char *hint = nullptr) {
  char buf[1024];
  snprintf(buf, sizeof buf, "%s", value.c_str());
  ImGui::SetNextItemWidth(width);
  int flags = secret ? ImGuiInputTextFlags_Password : 0;
  if (hint ? ImGui::InputTextWithHint(label, hint, buf, sizeof buf, flags)
           : ImGui::InputText(label, buf, sizeof buf, flags))
    value = buf;
}

void provider_row(const ProviderInfo &p) {
  Config &c = config();
  ServiceConfig &s = c.services[p.id];
  ImGui::PushID(p.id);
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  studio::Checkbox("##on", &s.enabled);
  ImGui::SameLine();
  ImGui::TextUnformatted(p.name);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nkey: %s", p.purpose, p.key_hint);
  ImGui::TableNextColumn();
  const bool needs_key = std::string(p.id) != "ollama" && std::string(p.id) != "comfyui";
  if (needs_key) {
    bool &reveal = g_reveal[p.id];
    text_field("##key", s.key, -34.f, !reveal, "API key");
    ImGui::SameLine();
    if (ImGui::SmallButton(reveal ? "hide" : "show")) reveal = !reveal;
  } else {
    ImGui::TextDisabled("no key needed");
  }
  ImGui::TableNextColumn();
  text_field("##ep", s.endpoint, -1.f, false, p.default_endpoint);
  ImGui::TableNextColumn();
  text_field("##model", s.model, -1.f, false, p.default_model[0] ? p.default_model : "model");
  ImGui::TableNextColumn();
  bool ready = service_ready(p.id);
  ImGui::TextColored(ready ? ImVec4(0.55f, 0.8f, 0.45f, 1.f) : ImVec4(0.5f, 0.5f, 0.5f, 1.f),
                     ready ? "ready" : "off");
  ImGui::PopID();
}

void default_combo(const char *label, std::string &value, const char *purpose_a,
                   const char *purpose_b) {
  ImGui::SetNextItemWidth(220);
  const ProviderInfo *cur = provider_info(value);
  if (ImGui::BeginCombo(label, cur ? cur->name : value.c_str())) {
    for (const ProviderInfo &p : known_providers()) {
      if (std::string(p.purpose) != purpose_a && std::string(p.purpose) != purpose_b) continue;
      bool ready = service_ready(p.id);
      if (!ready) ImGui::BeginDisabled();
      if (ImGui::Selectable(p.name, value == p.id)) value = p.id;
      if (!ready) ImGui::EndDisabled();
    }
    ImGui::EndCombo();
  }
}

} // namespace

void settings_services_tab(App &a) {
  (void)a;
  Config &c = config();
  ImGui::SeparatorText("Defaults");
  default_combo("Natural language", c.ai.text_provider, "text", "text");
  default_combo("Images, textures, skies", c.ai.image_provider, "image", "image");
  default_combo("3D models", c.ai.model_provider, "3d", "3d");
  ImGui::SeparatorText("Providers");
  ImGui::TextDisabled("Keys are stored protected for this Windows user in config.json. "
                      "An endpoint left empty uses the provider's own.");
  if (ImGui::BeginTable("##prov", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                                          ImGuiTableFlags_ScrollY)) {
    ImGui::TableSetupColumn("Service", 0, 1.1f);
    ImGui::TableSetupColumn("Key", 0, 1.6f);
    ImGui::TableSetupColumn("Endpoint", 0, 1.6f);
    ImGui::TableSetupColumn("Model", 0, 1.0f);
    ImGui::TableSetupColumn("", 0, 0.4f);
    ImGui::TableHeadersRow();
    for (const ProviderInfo &p : known_providers()) provider_row(p);
    ImGui::EndTable();
  }
}

void settings_comfy_tab(App &a) {
  (void)a;
  ComfyConfig &cf = config().comfy;
  ImGui::SeparatorText("Connection");
  const char *modes[] = {"Auto (local, then cloud)", "Local", "Cloud"};
  ImGui::SetNextItemWidth(220);
  ImGui::Combo("Mode", &cf.mode, modes, 3);
  text_field("Local server", cf.url, 320, false, "http://127.0.0.1:8188");
  text_field("Cloud server", cf.cloud_url, 320, false, "https://cloud.comfy.org");
  bool &reveal = g_reveal["comfy_cloud"];
  text_field("Cloud API key", cf.cloud_key, 320, !reveal, "X-API-Key");
  ImGui::SameLine();
  if (ImGui::SmallButton(reveal ? "hide" : "show")) reveal = !reveal;
  if (ImGui::Button("Check connection")) {
    std::string err;
    ComfyServerInfo info;
    g_check_result = comfy_probe(comfy_base_url(), info, err) ? "ComfyUI at " + comfy_base_url() + ": " +
                                                                    std::to_string(info.node_types) +
                                                                    " node types, " +
                                                                    std::to_string(info.checkpoints) +
                                                                    " checkpoints"
                                                              : "not reachable: " + err;
  }
  if (!g_check_result.empty()) {
    ImGui::SameLine();
    ImGui::TextWrapped("%s", g_check_result.c_str());
  }
  ImGui::SeparatorText("Installation");
  text_field("ComfyUI folder", cf.install_path, 420, false, "the folder that holds main.py");
  ImGui::SameLine();
  if (ImGui::SmallButton("Browse...")) {
    std::string p = dialog_open_file("ComfyUI main.py\0main.py\0All\0*.*\0", "py");
    if (!p.empty()) cf.install_path = p.substr(0, p.find_last_of("/\\"));
  }
  ImGui::TextDisabled("Workflows are read from <folder>/user/default/workflows and the folders below.");
  for (size_t i = 0; i < cf.workflow_dirs.size(); ++i) {
    ImGui::PushID((int)i);
    ImGui::TextUnformatted(cf.workflow_dirs[i].c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Remove")) {
      cf.workflow_dirs.erase(cf.workflow_dirs.begin() + (long)i);
      ImGui::PopID();
      break;
    }
    ImGui::PopID();
  }
  ImGui::SetNextItemWidth(420);
  ImGui::InputTextWithHint("##wf", "another folder of workflow .json files", g_wf_dir, sizeof g_wf_dir);
  ImGui::SameLine();
  if (ImGui::SmallButton("Add") && g_wf_dir[0]) {
    cf.workflow_dirs.push_back(g_wf_dir);
    g_wf_dir[0] = 0;
  }
  ImGui::SeparatorText("Jobs");
  ImGui::SetNextItemWidth(220);
  ImGui::SliderInt("Poll interval", &cf.poll_ms, 250, 5000, "%d ms");
  ImGui::SetNextItemWidth(220);
  ImGui::SliderInt("Timeout", &cf.timeout_s, 60, 7200, "%d s");
}

} // namespace studio
