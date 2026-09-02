// Geekatplay TerraForge — natural-language assistant shared by every tab.
// The model returns a small JSON action document; ai_apply_actions executes
// it. The scripting API and the MCP server call the same function, so text,
// script and tool calls all take one code path.
#include "ai_assist.hpp"
#include "app.hpp"
#include "ollama.hpp"
#include "prefs.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include "gpx/camera_math.hpp"
#include "gpx/serialization.hpp"
#include <imgui.h>
#include <json.hpp>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

using json = nlohmann::json;

namespace studio {

std::string dialog_open_file(const char *filter, const char *def_ext);
// CPU/GPU agreement check for field graphs (studio/field_gpu_check.cpp)
std::string field_gpu_verify_all(App &a);

// ---------------------------------------------------------------- UI bar
struct AiState {
  char prompt[512] = "";
  char image[512] = "";
  std::atomic<bool> running{false};
  std::mutex mtx;
  std::string result, status, error;
};

static AiState &state_for(AiDomain d) {
  static AiState s[6];
  return s[(int)d];
}

static const char *domain_name(AiDomain d) {
  switch (d) {
    case AiDomain::Camera: return "camera";
    case AiDomain::World: return "sky, sun, clouds, fog and water";
    case AiDomain::Material: return "material";
    case AiDomain::Terrain: return "terrain";
    case AiDomain::Object: return "scene object";
    default: return "render";
  }
}

static void run_assist(AiDomain domain, std::string prompt, std::string image) {
  AiState &st = state_for(domain);
  Prefs &p = prefs();
  std::string model = image.empty() ? p.text_model : p.vision_model;
  {
    std::lock_guard<std::mutex> lk(st.mtx);
    st.status = "asking " + model + "...";
    st.error.clear();
  }
  std::string sys = std::string("You control the ") + domain_name(domain) +
                    " settings of Geekatplay TerraForge, a 3D terrain studio.\n" +
                    ai_action_schema(domain);
  if (!image.empty())
    prompt += "\n(An image is attached: analyse it and match its lens, framing, "
              "light and mood.)";
  std::string out, err;
  bool ok = ollama_generate(p.ollama_url, model, sys, prompt, image, out, err);
  std::lock_guard<std::mutex> lk(st.mtx);
  if (ok) {
    st.result = out;
    st.status = "applying";
  } else {
    st.error = err;
    st.status.clear();
  }
  st.running.store(false);
}

void ai_assist_bar(App &a, AiDomain domain, const char *hint) {
  AiState &st = state_for(domain);
  ImGui::SeparatorText("Ask AI");
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##aiprompt", hint, st.prompt, sizeof st.prompt);
  ImGui::SetNextItemWidth(-118);
  ImGui::InputTextWithHint("##aiimg", "reference image (optional)", st.image,
                           sizeof st.image);
  ImGui::SameLine();
  if (ImGui::Button("image...", ImVec2(56, 0))) {
    std::string p = dialog_open_file(
        "Images\0*.png;*.jpg;*.jpeg;*.bmp\0All files\0*.*\0", nullptr);
    if (!p.empty()) snprintf(st.image, sizeof st.image, "%s", p.c_str());
  }
  ImGui::SameLine();
  if (ImGui::Button("clear", ImVec2(-1, 0))) st.image[0] = 0;

  bool busy = st.running.load();
  ImGui::BeginDisabled(busy || st.prompt[0] == 0);
  if (ImGui::Button(busy ? "thinking..." : "Apply", ImVec2(-1, 0))) {
    st.running.store(true);
    std::thread(run_assist, domain, std::string(st.prompt),
                std::string(st.image))
        .detach();
  }
  ImGui::EndDisabled();

  std::lock_guard<std::mutex> lk(st.mtx);
  if (!st.status.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.47f, 0.19f, 1.f));
    ImGui::TextWrapped("%s", st.status.c_str());
    ImGui::PopStyleColor();
  }
  if (!st.error.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.3f, 0.2f, 1.f));
    ImGui::TextWrapped("%s", st.error.c_str());
    ImGui::PopStyleColor();
  }
  if (!st.result.empty()) {
    std::string doc = std::move(st.result);
    st.result.clear();
    std::string err;
    if (ai_apply_actions(a, doc, err)) {
      st.status = "applied";
      a.status = "AI applied changes";
    } else {
      st.error = err;
      st.status.clear();
    }
  }
}

} // namespace studio


