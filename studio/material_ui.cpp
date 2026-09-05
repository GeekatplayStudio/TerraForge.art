// Geekatplay TerraForge - shared material logic. See material_ui.hpp.
#include "material_ui.hpp"
#include "app.hpp"
#include "gpx/serialization.hpp"
#include "material_stack_ops.hpp"
#include "undo.hpp"
#include <algorithm>
#include <cstring>
#include <functional>
#include <imgui.h>

namespace studio {

const char *material_type_name(int t) {
  static const char *N[MAT_TYPE_COUNT] = {"Simple",  "PBR textures", "Mixed",
                                          "Layered", "Distribution", "Effector"};
  return N[std::clamp(t, 0, MAT_TYPE_COUNT - 1)];
}

const char *material_type_blurb(int t) {
  static const char *B[MAT_TYPE_COUNT] = {
      "Channels fed directly: a colour, a noise, a picture.",
      "A photoscanned or painted texture set drives every channel.",
      "Two materials blended by a mask - rock through moss, sand into grass.",
      "Layers with their own presence rules, top layer first, like Photoshop.",
      "A layer whose presence also decides where objects stand - the material "
      "and the population share one rule.",
      "A typed field - pressure, wind, light, heat, moisture - other systems "
      "read; this material influences rather than colours."};
  return B[std::clamp(t, 0, MAT_TYPE_COUNT - 1)];
}

std::vector<MatEntry> collect_materials(App &a) {
  std::vector<MatEntry> out;
  for (auto &n : a.graph.nodes)
    if (n->type == "MaterialOutput") {
      std::string nm = n->attrs.get_s("name");
      if (nm.empty()) nm = "Material";
      out.push_back({n->id, nm});
    }
  return out;
}

int material_type_of(gpx::Graph &g, gpx::Node *mat) {
  if (!mat) return MAT_SIMPLE;
  // What feeds the base colour says what the material is; the other channels
  // follow it in every scaffold this file builds.
  gpx::Node *src = g.upstream_node(*mat, "base color");
  if (!src) return MAT_SIMPLE;
  if (src->type == "MaterialLayer") return MAT_LAYERED;
  if (src->type == "MaterialStack") return MAT_MIXED;
  if (src->type == "PBRMaterial") return MAT_PBR;
  if (src->type == "DistributionLayer") return MAT_DISTRIBUTION;
  if (src->type == "EffectorLayer") return MAT_EFFECTOR;
  return MAT_SIMPLE;
}

namespace {

// Put `node` between the material's `port` and whatever fed it: the old
// source now feeds node.`in`, and node.`out` feeds the material.
void interpose(gpx::Graph &g, gpx::Node *mat, const char *port, gpx::Node *node,
               const char *in, const char *out) {
  gpx::Link *old = layer_incoming(g, mat->id, port);
  if (old) {
    uint64_t from = old->from_node;
    std::string from_port = old->from_port;
    g.remove_link(old->id);
    g.add_link(from, from_port, node->id, in);
  }
  g.add_link(node->id, out, mat->id, port);
}

} // namespace

gpx::Node *material_set_type(App &a, gpx::Node *mat, int type) {
  if (!mat) return nullptr;
  gpx::Graph &g = a.graph;
  if (material_type_of(g, mat) == type) return mat;
  const float x = mat->pos_x - 260.f, y = mat->pos_y;
  gpx::Node *added = nullptr;
  switch (type) {
    case MAT_LAYERED: {
      std::vector<gpx::Node *> layers = collect_layers(g, mat);
      added = add_material_layer(g, mat, layers);
    } break;
    case MAT_MIXED: {
      added = g.add_node("MaterialStack", x, y);
      if (added) {
        interpose(g, mat, "base color", added, "albedo 1", "albedo");
        if (gpx::Link *r = layer_incoming(g, mat->id, "roughness"))
          g.remove_link(r->id);
        g.add_link(added->id, "roughness", mat->id, "roughness");
      }
    } break;
    case MAT_PBR: {
      added = g.add_node("PBRMaterial", x, y);
      if (added) {
        for (const char *port : {"base color", "normal", "roughness"})
          if (gpx::Link *l = layer_incoming(g, mat->id, port)) g.remove_link(l->id);
        g.add_link(added->id, "albedo", mat->id, "base color");
        g.add_link(added->id, "normal", mat->id, "normal");
        g.add_link(added->id, "roughness", mat->id, "roughness");
      }
    } break;
    case MAT_DISTRIBUTION: {
      added = g.add_node("DistributionLayer", x, y);
      if (added) interpose(g, mat, "base color", added, "albedo", "albedo");
    } break;
    case MAT_EFFECTOR: {
      added = g.add_node("EffectorLayer", x, y);
      if (added) interpose(g, mat, "base color", added, "albedo", "albedo");
    } break;
    case MAT_SIMPLE:
    default: {
      // Back to simple: the node feeding base colour is bypassed so whatever
      // fed IT reaches the material again. Nothing is deleted; the user can.
      gpx::Node *src = g.upstream_node(*mat, "base color");
      if (src) {
        src->enabled = false; // bypass: the graph resolves links straight through it
        added = src;
      }
    } break;
  }
  a.graph_layout_serial++;
  a.request_eval();
  a.uploaded_serial = 0;
  return added ? added : mat;
}

uint64_t material_fingerprint(gpx::Graph &g, uint64_t mat_id) {
  if (!mat_id) return 0;
  std::string j = gpx::material_to_json(g, mat_id);
  // FNV-1a over the text: cheap, and a material is a few kilobytes at most.
  uint64_t h = 1469598103934665603ull;
  for (unsigned char c : j) {
    h ^= c;
    h *= 1099511628211ull;
  }
  return h;
}

MaterialPreviewSpec material_preview_spec(App &a, gpx::Node *mat) {
  MaterialPreviewSpec s;
  if (!mat) return s;
  s.key = mat->id;
  s.version = a.eval_serial;
  auto chan = [&](const char *port) -> const void * {
    const gpx::TextureRGBA *t = mat->in_tex(port);
    return (t && !t->empty()) ? t : nullptr;
  };
  s.albedo = chan("base color");
  s.normal = chan("normal");
  s.rough = chan("roughness");
  s.roughness = mat->attrs.get_f("roughness", 0.85f);
  s.metallic = mat->attrs.get_f("metallic", 0.f);
  s.specular = mat->attrs.get_f("specular", 0.35f);
  s.reflection = mat->attrs.get_f("reflection", 0.25f);
  s.background = material_studio().background;
  return s;
}

void material_channel_row(App &a, gpx::Node *mat, const char *port,
                          const char *human) {
  gpx::Node *src = a.graph.upstream_node(*mat, port);
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::TextUnformatted(human);
  ImGui::TableNextColumn();
  if (src) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.75f, 0.45f, 1.f));
    ImGui::Text("%s #%llu", src->type.c_str(), (unsigned long long)src->id);
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Double-click to open it");
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
      graph_focus_node(a, src->id);
  } else {
    ImGui::TextDisabled("not connected");
  }
}

bool material_surface_ui(App &a, gpx::Node *mat, float label_w) {
  bool changed = false;
  auto slider = [&](const char *key, const char *label, float lo, float hi) {
    gpx::Attribute *at = mat->attrs.find(key);
    if (!at) return;
    ImGui::SetNextItemWidth(-label_w);
    if (ImGui::SliderFloat(label, &at->f, lo, hi)) changed = true;
  };
  slider("roughness", "Roughness", 0.02f, 1.f);
  slider("metallic", "Metallic", 0.f, 1.f);
  slider("specular", "Specular", 0.f, 1.f);
  slider("reflection", "Sky reflection", 0.f, 1.f);
  slider("translucency", "Translucency", 0.f, 1.f);
  slider("transparency", "Transparency", 0.f, 1.f);
  slider("normal_strength", "Normal strength", 0.f, 4.f);
  slider("displacement", "Displacement", 0.f, 0.1f);
  if (changed) {
    a.graph.mark_dirty(mat->id);
    a.request_eval();
    a.uploaded_serial = 0;
  }
  return changed;
}

MaterialStudioState &material_studio() {
  static MaterialStudioState s;
  return s;
}

bool material_studio_modified(App &a) {
  MaterialStudioState &st = material_studio();
  if (!st.material) return false;
  return material_fingerprint(a.graph, st.material) != st.saved_fingerprint;
}

void material_studio_mark_saved(App &a) {
  MaterialStudioState &st = material_studio();
  st.saved_fingerprint = material_fingerprint(a.graph, st.material);
}

bool material_studio_open(App &a, uint64_t mat_id) {
  MaterialStudioState &st = material_studio();
  if (st.material == mat_id) return true;
  if (material_studio_modified(a)) {
    // Ask before the change is lost from view. The prompt is drawn by the
    // studio panel; it opens `pending_open` once answered.
    st.pending_open = mat_id;
    gpx::Node *cur = a.graph.find_node(st.material);
    st.prompt_name = cur ? cur->attrs.get_s("name") : "this material";
    return false;
  }
  st.material = mat_id;
  st.saved_fingerprint = material_fingerprint(a.graph, mat_id);
  a.selected_node = mat_id;
  return true;
}

} // namespace studio
