// Geekatplay TerraForge — Properties: the Scene tab (project-wide settings). Split from panel_properties_object.cpp for the 500-line module rule.
#include "app.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include <algorithm>
#include <imgui.h>
#include <mutex>

namespace studio {

// Scene tab: project-wide settings
void scene_properties_ui(App &a) {
  RenderSettings &rs = render_settings();
  if (prop_filter_match("Resolution")) {
    ImGui::SeparatorText("Terrain resolution");
    int res = a.graph.resolution;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputInt("##res", &res, 64, 256,
                        ImGuiInputTextFlags_EnterReturnsTrue)) {
      std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
      if (lk.owns_lock()) {
        a.graph.resolution = std::clamp(res, 64, 8192);
        a.graph.mark_all_dirty();
        a.request_eval();
      }
    }
    ImGui::TextDisabled("Preview resolution of every node (64..8192).");
  }
  if (prop_filter_match("World scale")) {
    ImGui::SeparatorText("World scale");
    ImGui::SetNextItemWidth(-1);
    ImGui::DragFloat("##size", &rs.terrain_size_m, 50.f, 100.f, 100000.f,
                     "%.0f m across");
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##units", &rs.units, "Metric\0Imperial\0");
  }
  if (prop_filter_match("Layers")) {
    ImGui::SeparatorText("Layers");
    scene_layers_ui(a);
  }
  if (prop_filter_match("Statistics")) {
    ImGui::SeparatorText("Statistics");
    std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
    if (lk.owns_lock()) {
      double ms = 0;
      for (auto &n : a.graph.nodes) ms += n->last_compute_ms;
      ImGui::TextDisabled("%zu nodes, %zu links", a.graph.nodes.size(),
                          a.graph.links.size());
      ImGui::TextDisabled("last evaluation: %.0f ms", ms);
    }
    ImGui::TextDisabled("%zu scene objects", scene().objects.size());
  }
}

} // namespace studio
