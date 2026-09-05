// Geekatplay TerraForge — AI panel: describe a terrain in plain language
// (or drop in a photo) and a local Ollama model builds the node graph,
// materials and lighting.
#include "ai_services.hpp"
#include "app.hpp"
#include "ollama.hpp"
#include "prefs.hpp"
#include "render_settings.hpp"
#include "gpx/serialization.hpp"
#include <imgui.h>
#include <json.hpp>
#include <atomic>
#include <mutex>
#include <thread>

using json = nlohmann::json;

namespace studio {

std::string dialog_open_file(const char *filter, const char *def_ext);

static char ai_prompt[2048] =
    "Alpine mountain range with sharp ridges, deep river valleys carved by "
    "erosion, snow above the treeline, late afternoon sun with light haze.";
static char ai_image[512] = "";
static std::atomic<bool> ai_running{false};
static std::mutex ai_mtx;
static std::string ai_result, ai_error, ai_status;

static std::string build_system_prompt() {
  std::string s =
      "You are the terrain-graph planner inside Geekatplay TerraForge, a "
      "node-based terrain generator. Convert the user's description (and "
      "photo, if given) into a node graph.\n\n"
      "Respond with ONLY a JSON object, no prose, in this exact shape:\n"
      "{\"nodes\":[{\"id\":\"base\",\"type\":\"Noise\",\"attrs\":{\"type\":"
      "\"Ridged\",\"octaves\":10},\"pos\":[0,100]}],\n"
      " \"links\":[[\"base\",\"output\",\"erode\",\"input\"]],\n"
      " \"environment\":{\"sun_azimuth\":220,\"sun_altitude\":25,"
      "\"fog_type\":1,\"fog_density\":1.2,\"water_level\":0.1,"
      "\"snowy_sun\":false}}\n\n"
      "Rules:\n"
      "- Always build a full chain: primitive -> (warp/filters) -> erosion -> "
      "TerrainTexture. Link TerrainTexture 'input' to the final heightmap and "
      "prefer connecting a StreamPower/Hydraulic flow or erosion_map output "
      "to its 'flow' input for realistic wet valleys.\n"
      "- Choose attrs that match the description (e.g. desert: Wind erosion, "
      "sand colors, high beach_level; islands: Shape 'Border falloff' as "
      "Noise envelope).\n"
      "- Use pos with x increasing 260 per column so the graph reads left to "
      "right.\n"
      "- environment keys (all optional): sun_azimuth(0-360), "
      "sun_altitude(1-89), sun_intensity(0.2-8), sun_color([r,g,b] 0-1), "
      "fog_type(0=off,1=haze,2=fog,3=pollution), fog_density(0-6), "
      "fog_level(0-1), fog_color([r,g,b]), atmosphere_density(0.05-3), "
      "water_level(0-1 or 0 for none), water_deep_color, water_shallow_color, "
      "height_scale(0.02-0.8).\n\n"
      "Available nodes (name (category): description, ports, attrs):\n";
  s += gpx::registry_catalog_for_ai();
  return s;
}

static void apply_environment(const std::string &env_json) {
  if (env_json.empty()) return;
  json j;
  try {
    j = json::parse(env_json);
  } catch (...) {
    return;
  }
  RenderSettings &rs = render_settings();
  auto f = [&](const char *k, float &dst, float lo, float hi) {
    if (j.contains(k) && j[k].is_number())
      dst = std::clamp(j[k].get<float>(), lo, hi);
  };
  auto col = [&](const char *k, float *dst) {
    if (j.contains(k) && j[k].is_array() && j[k].size() >= 3)
      for (int i = 0; i < 3; ++i)
        dst[i] = std::clamp(j[k][i].get<float>(), 0.f, 1.f);
  };
  rs.sun_mode = 0;
  f("sun_azimuth", rs.sun_azimuth, 0, 360);
  f("sun_altitude", rs.sun_altitude, 1, 89);
  f("sun_intensity", rs.sun_intensity, 0.2f, 8);
  col("sun_color", rs.sun_color);
  if (j.contains("fog_type") && j["fog_type"].is_number())
    rs.fog_type = std::clamp(j["fog_type"].get<int>(), 0, 3);
  f("fog_density", rs.fog_density, 0, 6);
  f("fog_level", rs.fog_level, 0, 1);
  col("fog_color", rs.fog_color);
  f("atmosphere_density", rs.atmosphere_density, 0.05f, 3);
  if (j.contains("water_level") && j["water_level"].is_number()) {
    float wl = j["water_level"].get<float>();
    rs.show_water = wl > 0.001f;
    rs.water_level = std::clamp(wl, 0.f, 1.f);
  }
  col("water_deep_color", rs.water_deep_color);
  col("water_shallow_color", rs.water_shallow_color);
  f("height_scale", rs.height_scale, 0.02f, 0.8f);
}

static void run_generation(std::string prompt, std::string image) {
  Prefs &p = prefs();
  std::string model = image.empty() ? p.text_model : p.vision_model;
  {
    std::lock_guard<std::mutex> lk(ai_mtx);
    ai_status = "asking " + model + "...";
    ai_error.clear();
  }
  if (!image.empty())
    prompt += "\n(An image is attached: reproduce this landscape's shapes, "
              "materials, lighting and atmosphere as closely as possible.)";
  std::string out, err;
  bool ok = ai_text(std::string(), build_system_prompt(), prompt,
                            image, out, err);
  std::lock_guard<std::mutex> lk(ai_mtx);
  if (ok) {
    ai_result = out;
    ai_status = "response received — building graph";
  } else {
    ai_error = err;
    ai_status.clear();
  }
  ai_running.store(false);
}

void draw_panel_ai(App &a) {
  if (!ImGui::Begin("AI")) {
    ImGui::End();
    return;
  }
  ImGui::TextDisabled("Describe the terrain; a local AI builds the node graph,");
  ImGui::TextDisabled("materials and lighting. Optionally attach a photo.");
  ImGui::Spacing();
  ImGui::InputTextMultiline("##prompt", ai_prompt, sizeof ai_prompt,
                            ImVec2(-1, ImGui::GetTextLineHeight() * 6));
  ImGui::SetNextItemWidth(-70);
  ImGui::InputTextWithHint("##img", "photo (optional)...", ai_image,
                           sizeof ai_image);
  ImGui::SameLine(0, 2);
  if (ImGui::Button("...", ImVec2(28, 0))) {
    std::string p = dialog_open_file(
        "Images\0*.png;*.jpg;*.jpeg;*.bmp\0All files\0*.*\0", nullptr);
    if (!p.empty()) snprintf(ai_image, sizeof ai_image, "%s", p.c_str());
  }
  ImGui::SameLine(0, 2);
  if (ImGui::Button("clear", ImVec2(-1, 0))) ai_image[0] = 0;

  bool busy = ai_running.load();
  ImGui::BeginDisabled(busy);
  if (ImGui::Button(ai_image[0] ? "Generate from photo + text" : "Generate terrain",
                    ImVec2(-1, 0))) {
    ai_running.store(true);
    std::thread(run_generation, std::string(ai_prompt), std::string(ai_image))
        .detach();
  }
  ImGui::EndDisabled();

  {
    std::lock_guard<std::mutex> lk(ai_mtx);
    if (busy || !ai_status.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.47f, 0.19f, 1.f));
      ImGui::TextWrapped("%s", busy ? (ai_status + " (model may take a while)").c_str()
                                    : ai_status.c_str());
      ImGui::PopStyleColor();
    }
    if (!ai_error.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.3f, 0.2f, 1.f));
      ImGui::TextWrapped("%s", ai_error.c_str());
      ImGui::PopStyleColor();
      ImGui::TextDisabled("Ollama must be running locally (ollama.com);\n"
                          "set URL and models in Edit > Preferences.");
    }
    // apply a finished result on the UI thread
    if (!ai_result.empty()) {
      std::string spec = std::move(ai_result);
      ai_result.clear();
      std::string err, env;
      std::lock_guard<std::mutex> glk(a.graph_mtx);
      gpx::Graph fresh;
      fresh.resolution = a.graph.resolution;
      if (gpx::graph_from_ai_spec(fresh, spec, err, &env)) {
        a.graph.adopt(fresh);
        apply_environment(env);
        a.selected_node = a.view_node = 0;
        a.graph_layout_serial++;
        a.graph.mark_all_dirty();
        a.request_eval();
        ai_status = "graph built: " + std::to_string(a.graph.nodes.size()) +
                    " nodes — computing";
      } else {
        ai_error = err;
        ai_status.clear();
      }
    }
  }
  ImGui::End();
}

} // namespace studio
