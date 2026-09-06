// Geekatplay TerraForge - an EcosystemLayer's tabs in the Material Studio,
// as Vue lays its EcoSystem material out (manual p1088-1109): General,
// Density, Scaling & Orientation, Color, Presence, Animation - with the
// affinity and repulsion dials where Vue puts them, on the General tab's
// distribution block, and the layer below named so the artist knows what
// the population reacts to. Each dial is the node's own attribute, so the
// label, range and tooltip live in engine/nodes/scatter_attrs.hpp.
#include "app.hpp"
#include "material_stack_ops.hpp"
#include "material_ui.hpp"
#include "scene.hpp"
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

// the population this layer reacts to, by name, or why there is none
std::string below_name(App &a, gpx::Node *eco) {
  gpx::Node *b = a.graph.upstream_node(*eco, "below");
  if (!b) return "no population below this layer: affinity and repulsion do nothing";
  std::string nm = b->attrs.get_s("name");
  if (nm.empty()) nm = b->type;
  return "reacts to the layer below: " + nm;
}

int bound_meshes(App &a, gpx::Node *eco) {
  int n = 0;
  for (const SceneObject &o : scene().objects)
    n += o.type == SceneObject::Mesh && o.scatter_node == eco->id;
  (void)a;
  return n;
}

void tab_general(App &a, gpx::Node *e) {
  keys(a, e, {"name", "enabled"});
  ImGui::SeparatorText("Population");
  const int species = e->attrs.get_i("species", 1);
  material_attr_widget(a, e, "species", LW);
  for (int s = 1; s <= species; ++s) {
    std::string k = "sp" + std::to_string(s);
    keys(a, e, {(k + "_presence").c_str(), (k + "_scale").c_str()});
  }
  const int meshes = bound_meshes(a, e);
  ImGui::TextDisabled("%d mesh%s bound to this layer (Properties > Scatter, Points node = this layer)",
                      meshes, meshes == 1 ? "" : "es");
  ImGui::SeparatorText("Distribution");
  ImGui::TextDisabled("%s", below_name(a, e).c_str());
  group(a, e, "Interaction");
}

void tab_density(App &a, gpx::Node *e) {
  ImGui::SeparatorText("Overall density");
  group(a, e, "Density");
  ImGui::SeparatorText("Decay near foreign objects");
  material_channel_ui(a, e, "objects", "Objects", CHAN_VALUE, nullptr);
  ImGui::TextDisabled("Connect TerrainImprint's 'objects' output: the distance to every object standing on the terrain.");
  group(a, e, "Objects");
}

void tab_scaling(App &a, gpx::Node *e) {
  group(a, e, "Scaling");
  ImGui::SeparatorText("Driver");
  material_channel_ui(a, e, "driver", "Driver", CHAN_VALUE, nullptr);
  ImGui::TextDisabled("A map that picks the species by interval and, with Rotation = Driven, the turn.");
}

void tab_color(App &a, gpx::Node *e) {
  group(a, e, "Color");
  ImGui::SeparatorText("Animation");
  group(a, e, "Animation");
}

void tab_presence(App &a, gpx::Node *e) {
  ImGui::TextDisabled("Where this population appears, by the environment.");
  material_channel_ui(a, e, "mask", "Presence (mask)", CHAN_ALPHA, nullptr);
  keys(a, e, {"invert_mask"});
  material_channel_ui(a, e, "terrain", "Terrain", CHAN_VALUE, nullptr);
  ImGui::SeparatorText("Altitude constraint");
  group(a, e, "Altitude");
  ImGui::SeparatorText("Slope constraint");
  group(a, e, "Slope");
  ImGui::SeparatorText("Orientation constraint");
  group(a, e, "Orientation");
  if (unsigned tex = previews_get(e->id)) {
    ImGui::SeparatorText("Density map");
    ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(128, 128));
  }
}

struct Tab {
  const char *name;
  void (*fn)(App &, gpx::Node *);
};

} // namespace

void material_tabs_ecosystem_ui(App &a, gpx::Node *eco) {
  static const Tab tabs[] = {{"General", tab_general},
                             {"Density", tab_density},
                             {"Scaling & Orientation", tab_scaling},
                             {"Color", tab_color},
                             {"Presence", tab_presence}};
  for (const Tab &t : tabs)
    if (ImGui::BeginTabItem(t.name)) {
      ImGui::BeginChild("##tab", ImVec2(0, 0));
      t.fn(a, eco);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
}

} // namespace studio
