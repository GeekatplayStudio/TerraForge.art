// Geekatplay TerraForge - the Material Editor's tabs for a material or a
// layer, as Vue lays them out (manual p703: Color, Alpha, Bump, Normal,
// Displacement, Highlights, Transparency, Reflection, Translucency, Effects,
// Presence; p742-744 for PBR: Ambient Occlusion, Metalness, Roughness,
// Clearcoat). Each channel tab starts with the channel's mode block
// (material_channel_ui.cpp) and continues with that tab's parameters, read
// from the node's attribute groups so the label, range and tooltip live in
// one place (engine/material_params.cpp).
#include "app.hpp"
#include "material_channel_ops.hpp"
#include "material_ui.hpp"
#include "undo.hpp"
#include <imgui.h>
#include <string>

namespace studio {

namespace {

const float LW = 170.f;

void group(App &a, gpx::Node *n, const char *g) {
  for (gpx::Attribute &at : n->attrs.items)
    if (at.group == g) material_attr_widget(a, n, at.key.c_str(), LW);
}
void keys(App &a, gpx::Node *n, std::initializer_list<const char *> ks) {
  for (const char *k : ks) material_attr_widget(a, n, k, LW);
}

// ---- the root material's tabs -------------------------------------------
void tab_color(App &a, gpx::Node *m) {
  material_channel_ui(a, m, "base color", "Color", CHAN_COLOR, "tint");
  ImGui::SeparatorText("Color correction");
  group(a, m, "Color");
}
void tab_alpha(App &a, gpx::Node *m) {
  material_channel_ui(a, m, "alpha", "Alpha", CHAN_ALPHA, "alpha");
  ImGui::TextDisabled("Alpha does not bend light; transparency does. White is transparent.");
}
void tab_bump(App &a, gpx::Node *m) {
  material_channel_ui(a, m, "normal", "Normal map", CHAN_NORMAL, nullptr);
  ImGui::SeparatorText("Parameters");
  keys(a, m, {"normal_strength", "bump_depth", "bump_slope", "normal_invert"});
}
void tab_displacement(App &a, gpx::Node *m) {
  material_channel_ui(a, m, "height", "Displacement", CHAN_VALUE, nullptr);
  ImGui::SeparatorText("Parameters");
  keys(a, m, {"displacement", "disp_smoothing"});
  ImGui::TextDisabled("Depth is in world units. Moves geometry, not only normals.");
}
void tab_highlights(App &a, gpx::Node *m, bool pbr) {
  material_channel_ui(a, m, "roughness", "Highlights map", CHAN_VALUE, nullptr);
  ImGui::SeparatorText("Parameters");
  if (pbr) {
    keys(a, m, {"specular"});
    ImGui::TextDisabled("PBR: the shape of the highlights is the Roughness channel.");
  } else {
    group(a, m, "Highlights");
  }
}
void tab_transparency(App &a, gpx::Node *m) { group(a, m, "Transparency"); }
void tab_reflection(App &a, gpx::Node *m, bool pbr) {
  if (pbr) keys(a, m, {"specular_level", "reflection", "reflect_blur"});
  else keys(a, m, {"reflection", "reflect_min", "reflect_angle", "reflect_blur"});
}
void tab_translucency(App &a, gpx::Node *m) { group(a, m, "Translucency"); }
void tab_effects(App &a, gpx::Node *m, bool pbr) {
  ImGui::SeparatorText("Lighting");
  if (pbr) keys(a, m, {"luminous", "luminous_color", "backlight"});
  else group(a, m, "Effects");
  ImGui::SeparatorText("Global transformation");
  group(a, m, "Transform");
}
void tab_ao(App &a, gpx::Node *m) {
  material_channel_ui(a, m, "ambient occlusion", "Ambient occlusion", CHAN_VALUE, nullptr);
  ImGui::TextDisabled("How much incoming light reaches a point. Affects the diffuse only.");
}
void tab_metalness(App &a, gpx::Node *m) {
  material_channel_ui(a, m, "metallic", "Metalness", CHAN_VALUE, "metallic");
  ImGui::TextDisabled("1 raw metal (colour is the reflectance), 0 non-metal (colour is the albedo).");
}
void tab_roughness(App &a, gpx::Node *m) {
  material_channel_ui(a, m, "roughness", "Roughness", CHAN_VALUE, "roughness");
  ImGui::TextDisabled("0 a perfect mirror, 1 fully diffuse; between, glossy.");
}
void tab_clearcoat(App &a, gpx::Node *m) { group(a, m, "Clearcoat"); }

// ---- a MaterialLayer's tabs ---------------------------------------------
void layer_color(App &a, gpx::Node *l) {
  material_channel_ui(a, l, "albedo", "Color", CHAN_COLOR, nullptr);
  ImGui::SeparatorText("Layer");
  keys(a, l, {"name", "blend"});
}
void layer_alpha(App &a, gpx::Node *l) {
  material_channel_ui(a, l, "mask", "Alpha (mask)", CHAN_ALPHA, "opacity");
  keys(a, l, {"invert_mask", "alpha_boost"});
}
void layer_bump(App &a, gpx::Node *l) {
  material_channel_ui(a, l, "normal", "Normal map", CHAN_NORMAL, nullptr);
  keys(a, l, {"normal_add"});
}
void layer_highlights(App &a, gpx::Node *l) {
  material_channel_ui(a, l, "roughness", "Roughness", CHAN_VALUE, "rough_value");
}
void layer_effects(App &a, gpx::Node *l) {
  ImGui::SeparatorText("Placement");
  group(a, l, "Placement");
}

struct Tab {
  const char *name;
  void (*fn)(App &, gpx::Node *);
};

void draw_tabs(App &a, gpx::Node *n, const std::vector<Tab> &tabs) {
  for (const Tab &t : tabs)
    if (ImGui::BeginTabItem(t.name)) {
      ImGui::BeginChild("##tab", ImVec2(0, 0));
      t.fn(a, n);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
}

} // namespace

void material_tabs_ui(App &a, gpx::Node *mat) {
  MaterialStudioState &st = material_studio();
  gpx::Node *sel = a.graph.find_node(st.selected);
  if (!sel) sel = mat;
  if (!ImGui::BeginTabBar("##mattabs", ImGuiTabBarFlags_FittingPolicyScroll)) return;

  if (sel->type == "MaterialOutput") {
    const int type = material_type_of(a.graph, mat);
    const bool pbr = type == MAT_PBR;
    static bool s_pbr;
    s_pbr = pbr;
    std::vector<Tab> tabs = {
        {"Color", tab_color},
        {"Alpha", tab_alpha},
        {"Bump", tab_bump},
        {"Displacement", tab_displacement},
        {"Highlights", [](App &aa, gpx::Node *m) { tab_highlights(aa, m, s_pbr); }},
    };
    if (!pbr) tabs.push_back({"Transparency", tab_transparency});
    tabs.push_back({"Reflection", [](App &aa, gpx::Node *m) { tab_reflection(aa, m, s_pbr); }});
    if (!pbr) tabs.push_back({"Translucency", tab_translucency});
    if (pbr) {
      tabs.push_back({"Ambient Occlusion", tab_ao});
      tabs.push_back({"Metalness", tab_metalness});
      tabs.push_back({"Roughness", tab_roughness});
      tabs.push_back({"Clearcoat", tab_clearcoat});
    }
    tabs.push_back({"Effects", [](App &aa, gpx::Node *m) { tab_effects(aa, m, s_pbr); }});
    draw_tabs(a, sel, tabs);
    if ((type == MAT_DISTRIBUTION || type == MAT_EFFECTOR) &&
        ImGui::BeginTabItem(type == MAT_DISTRIBUTION ? "Population" : "Effector")) {
      ImGui::BeginChild("##tab");
      if (gpx::Node *src = a.graph.upstream_node(*mat, "base color"))
        material_tab_population_ui(a, src);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
  } else if (sel->type == "MaterialLayer") {
    draw_tabs(a, sel,
              {{"Color", layer_color},
               {"Alpha", layer_alpha},
               {"Bump", layer_bump},
               {"Highlights", layer_highlights},
               {"Effects", layer_effects}});
    if (ImGui::BeginTabItem("Presence")) {
      ImGui::BeginChild("##tab");
      material_tab_presence_ui(a, sel);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
  } else if (sel->type == "MaterialStack") {
    material_tabs_mix_ui(a, sel);
  } else {
    // a sub-material or a driving node: its own parameters, and a way in
    if (ImGui::BeginTabItem(sel->type.c_str())) {
      ImGui::BeginChild("##tab");
      material_tab_population_ui(a, sel);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
  }
  ImGui::EndTabBar();
}

} // namespace studio
