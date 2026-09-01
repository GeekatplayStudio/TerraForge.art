// Geekatplay TerraForge — Material editor.
// Substance-style channel graph on a MaterialOutput node, Blender-style
// per-object assignment, live turntable preview (sphere/cube/flat), an
// on-disk library with thumbnails, PBR texture-set import, and AI material
// generation. Design choices address the top complaints about Substance
// (stale/opaque library, downstream recompute stalls) and Blender
// (unreliable previews, append/link confusion): thumbnails regenerate on
// every save and loading always creates an independent copy.
#include "app.hpp"
#include "material_library.hpp"
#include "ollama.hpp"
#include "prefs.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "gpx/serialization.hpp"
#include <imgui.h>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace studio {

std::string dialog_open_file(const char *filter, const char *def_ext);

struct MatEntry {
  uint64_t id;
  std::string name;
};

static std::vector<MatEntry> collect_materials(App &a) {
  std::vector<MatEntry> out;
  for (auto &n : a.graph.nodes)
    if (n->type == "MaterialOutput") {
      std::string nm = n->attrs.get_s("name");
      if (nm.empty()) nm = "Material";
      out.push_back({n->id, nm + "  #" + std::to_string(n->id)});
    }
  return out;
}

static void channel_row(App &a, gpx::Node *mat, const char *port,
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
  } else {
    ImGui::TextDisabled("not connected");
  }
}

// ---- AI material generation -------------------------------------------
static std::atomic<bool> matai_running{false};
static std::mutex matai_mtx;
static std::string matai_result, matai_error, matai_status;

static std::string material_ai_system_prompt() {
  std::string s =
      "You are the material designer inside Geekatplay TerraForge. Build a "
      "PBR material as a node graph from the user's description.\n\n"
      "Respond with ONLY a JSON object:\n"
      "{\"nodes\":[{\"id\":\"n1\",\"type\":\"Noise\",\"attrs\":{...},"
      "\"pos\":[0,600]}, ...],\n"
      " \"links\":[[\"n1\",\"output\",\"n2\",\"input\"], ...]}\n\n"
      "Rules:\n"
      "- The graph MUST end in exactly one MaterialOutput node. Set its "
      "'name' attr to a short material name.\n"
      "- Connect at least 'base color'; also drive 'normal', 'roughness', "
      "'height' and 'ambient occlusion' when they help realism.\n"
      "- Typical channel chain: Noise -> (filters) -> MaskToTexture or "
      "TerrainTexture/GradientMap for color; the same height field through "
      "NormalMap for the normal channel and AOFromHeight for occlusion.\n"
      "- Use Levels for contrast, GradientMap for coloring by value, "
      "TextureTransform for tiling. Set MaterialOutput surface attrs "
      "(roughness, displacement...) to match the material.\n"
      "- Use pos x increasing 260 per column, y around 600 so the material "
      "sits below the terrain graph.\n\n"
      "Available nodes:\n";
  s += gpx::registry_catalog_for_ai(
      {"Material", "Texture", "Primitive", "Filter", "Mask", "Operator", "Logic"});
  return s;
}

static void run_material_ai(std::string prompt) {
  Prefs &p = prefs();
  {
    std::lock_guard<std::mutex> lk(matai_mtx);
    matai_status = "asking " + p.text_model + "...";
    matai_error.clear();
  }
  std::string out, err;
  bool ok = ollama_generate(p.ollama_url, p.text_model,
                            material_ai_system_prompt(),
                            "Create this material: " + prompt, "", out, err);
  std::lock_guard<std::mutex> lk(matai_mtx);
  if (ok) {
    matai_result = out;
    matai_status = "building material graph";
  } else {
    matai_error = err;
    matai_status.clear();
  }
  matai_running.store(false);
}

static void material_ai_ui(App &a, SceneObject &obj) {
  ImGui::SeparatorText("AI material");
  static char prompt[512] = "old tree bark, deep cracks, green moss in crevices";
  ImGui::InputTextMultiline("##matai", prompt, sizeof prompt,
                            ImVec2(-1, ImGui::GetTextLineHeight() * 3));
  bool busy = matai_running.load();
  ImGui::BeginDisabled(busy);
  if (ImGui::Button(busy ? "generating..." : "Generate material with AI",
                    ImVec2(-1, 0))) {
    matai_running.store(true);
    std::thread(run_material_ai, std::string(prompt)).detach();
  }
  ImGui::EndDisabled();
  std::lock_guard<std::mutex> lk(matai_mtx);
  if (busy || !matai_status.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.47f, 0.19f, 1.f));
    ImGui::TextWrapped("%s", matai_status.c_str());
    ImGui::PopStyleColor();
  }
  if (!matai_error.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.3f, 0.2f, 1.f));
    ImGui::TextWrapped("%s", matai_error.c_str());
    ImGui::PopStyleColor();
  }
  if (!matai_result.empty()) {
    std::string spec = std::move(matai_result);
    matai_result.clear();
    std::string err;
    std::lock_guard<std::mutex> glk(a.graph_mtx);
    size_t before = a.graph.nodes.size();
    if (gpx::graph_from_ai_spec(a.graph, spec, err, nullptr, /*merge=*/true)) {
      // assign the freshly created MaterialOutput to this object
      for (size_t i = before; i < a.graph.nodes.size(); ++i)
        if (a.graph.nodes[i]->type == "MaterialOutput")
          obj.material_node = a.graph.nodes[i]->id;
      a.graph_layout_serial++;
      a.request_eval();
      matai_status = "material graph created";
    } else {
      matai_error = err;
      matai_status.clear();
    }
  }
}

// ---- library grid ------------------------------------------------------
static void library_ui(App &a, SceneObject &obj, gpx::Node *mat) {
  ImGui::SeparatorText("Library");
  {
    // save / import row
    ImGui::BeginDisabled(mat == nullptr);
    if (ImGui::Button("Save to library", ImVec2(-1, 0))) {
      std::string err;
      std::string path = material_library_save(a, mat->id, err);
      a.status = path.empty() ? ("SAVE FAILED: " + err)
                              : ("material saved: " + path);
    }
    ImGui::EndDisabled();
    if (ImGui::Button("Import PBR texture set...", ImVec2(-1, 0))) {
      std::string p = dialog_open_file(
          "Textures\0*.png;*.jpg;*.jpeg;*.tga;*.bmp\0All files\0*.*\0", nullptr);
      if (!p.empty()) {
        std::string err;
        unsigned long long id = material_import_texture_set(a, p, err);
        if (id) {
          obj.material_node = id;
          a.status = "texture set imported as material";
        } else {
          a.status = "IMPORT FAILED: " + err;
        }
      }
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Pick any texture in a folder; base color, normal,\n"
                        "roughness, metallic, height and AO are detected by\n"
                        "the standard suffix conventions (ambientCG,\n"
                        "Poly Haven, Quixel, Substance exports). Glossiness\n"
                        "maps are inverted automatically.");
  }

  // thumbnail grid; click applies, right-click shows full size
  auto &lib = material_library();
  if (lib.empty()) {
    ImGui::TextDisabled("library is empty — save a material to start it");
    return;
  }
  static int popup_idx = -1;
  const float cell = 74.f;
  float avail = ImGui::GetContentRegionAvail().x;
  int cols = std::max(1, (int)(avail / (cell + 8)));
  int i = 0;
  for (auto &m : lib) {
    if (i % cols != 0) ImGui::SameLine();
    ImGui::PushID(i);
    ImGui::BeginGroup();
    unsigned tex = material_thumb_texture(m);
    bool clicked;
    if (tex)
      clicked = ImGui::ImageButton("##t", (ImTextureID)(intptr_t)tex,
                                   ImVec2(cell, cell));
    else
      clicked = ImGui::Button(m.name.c_str(), ImVec2(cell, cell));
    if (clicked) {
      std::string err;
      unsigned long long id = material_library_load(a, m, err);
      if (id) {
        obj.material_node = id;
        a.status = "applied " + m.name;
      } else {
        a.status = "LOAD FAILED: " + err;
      }
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s\nclick: apply a copy to this object\n"
                        "right-click: full-size preview", m.name.c_str());
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) popup_idx = i;
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cell);
    ImGui::TextUnformatted(m.name.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::PopID();
    ++i;
  }
  if (popup_idx >= 0) ImGui::OpenPopup("mat_full");
  if (ImGui::BeginPopup("mat_full")) {
    if (popup_idx >= 0 && popup_idx < (int)lib.size()) {
      unsigned tex = material_thumb_texture(lib[popup_idx]);
      ImGui::TextUnformatted(lib[popup_idx].name.c_str());
      if (tex)
        ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(384, 384));
      if (ImGui::Button("delete from library", ImVec2(-1, 0))) {
        std::error_code ec;
        std::filesystem::remove(lib[popup_idx].path, ec);
        if (!lib[popup_idx].thumb.empty())
          std::filesystem::remove(lib[popup_idx].thumb, ec);
        material_library_rescan();
        popup_idx = -1;
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndPopup();
  } else {
    popup_idx = -1;
  }
}

static void material_editor(App &a, SceneObject &obj) {
  std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
  if (!lk.owns_lock()) {
    ImGui::TextDisabled("computing...");
    return;
  }
  std::vector<MatEntry> mats = collect_materials(a);
  gpx::Node *mat = a.graph.find_node(obj.material_node);
  if (mat && mat->type != "MaterialOutput") mat = nullptr;

  // ---- material slot ----
  ImGui::SeparatorText("Material");
  ImGui::SetNextItemWidth(-92);
  std::string label = mat ? mat->attrs.get_s("name") + "  #" +
                                std::to_string(mat->id)
                          : std::string("(none)");
  if (ImGui::BeginCombo("##matsel", label.c_str())) {
    if (ImGui::Selectable("(none)", mat == nullptr)) {
      obj.material_node = 0;
      a.uploaded_serial = 0;
    }
    for (const MatEntry &m : mats)
      if (ImGui::Selectable(m.name.c_str(), mat && m.id == mat->id)) {
        obj.material_node = m.id;
        a.uploaded_serial = 0;
      }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button("New", ImVec2(-1, 0))) {
    float x = 900, y = 120;
    for (auto &n : a.graph.nodes)
      if (n->type == "MaterialOutput") x = std::max(x, n->pos_x + 260);
    gpx::Node *nn = a.graph.add_node("MaterialOutput", x, y);
    if (nn) {
      gpx::Attribute *na = nn->attrs.find("name");
      if (na) na->s = obj.name + " material";
      obj.material_node = nn->id;
      a.selected_node = nn->id;
      a.graph_layout_serial++;
      a.workspace = 1;
      a.request_eval();
    }
  }

  if (mat) {
    char buf[128];
    snprintf(buf, sizeof buf, "%s", mat->attrs.get_s("name").c_str());
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##matname", buf, sizeof buf)) {
      gpx::Attribute *na = mat->attrs.find("name");
      if (na) na->s = buf;
    }
  }

  // ---- live preview: sphere / cube / flat, turntable, drag to spin ----
  ImGui::SeparatorText("Preview");
  static int shape = 0;
  static float spin = 0.6f;
  static bool turntable = true;
  const char *shape_names[3] = {"Sphere", "Cube", "Flat"};
  for (int s = 0; s < 3; ++s) {
    bool active = shape == s;
    if (active)
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.33f, 0.13f, 1.f));
    if (ImGui::SmallButton(shape_names[s])) shape = s;
    if (active) ImGui::PopStyleColor();
    ImGui::SameLine();
  }
  studio::Checkbox("spin", &turntable);
  if (turntable) spin += ImGui::GetIO().DeltaTime * 0.5f;
  float avail = ImGui::GetContentRegionAvail().x;
  float side = std::min(avail, 220.f);
  unsigned tex = renderer_material_preview((int)std::max(side, 64.f), shape, spin);
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - side) * 0.5f);
  ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(side, side), ImVec2(0, 1),
               ImVec2(1, 0));
  if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.f)) {
    spin += ImGui::GetIO().MouseDelta.x * 0.01f;
    turntable = false;
  }
  // Double-clicking the preview opens the material in the node editor - the
  // shortest path from "this does not look right" to the graph that made it.
  if (ImGui::IsItemHovered() &&
      ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && obj.material_node)
    graph_focus_node(a, obj.material_node);
  ImGui::TextDisabled("Lit with the scene sun and sky. Drag to rotate;\n"
                      "double-click to open it in the node editor.");

  if (!mat) {
    ImGui::TextDisabled("No material assigned — create, import or generate one.");
  } else {
    // ---- channels ----
    ImGui::SeparatorText("Channels");
    if (ImGui::BeginTable("chan", 2, ImGuiTableFlags_SizingStretchProp)) {
      channel_row(a, mat, "base color", "Base color");
      channel_row(a, mat, "normal", "Normal");
      channel_row(a, mat, "roughness", "Roughness");
      channel_row(a, mat, "metallic", "Metallic");
      channel_row(a, mat, "height", "Height / displacement");
      channel_row(a, mat, "ambient occlusion", "Ambient occlusion");
      ImGui::EndTable();
    }
    if (ImGui::Button("Edit channels in the node graph", ImVec2(-1, 0))) {
      a.workspace = 1;
      a.selected_node = mat->id;
      a.prop_tab = TAB_NODE;
    }

    // ---- surface ----
    ImGui::SeparatorText("Surface");
    bool changed = false;
    auto slider = [&](const char *key, const char *label2, float lo, float hi) {
      gpx::Attribute *at = mat->attrs.find(key);
      if (!at) return;
      ImGui::SetNextItemWidth(-130);
      if (ImGui::SliderFloat(label2, &at->f, lo, hi)) changed = true;
    };
    slider("roughness", "Roughness", 0.02f, 1.f);
    slider("metallic", "Metallic", 0.f, 1.f);
    slider("specular", "Specular", 0.f, 1.f);
    slider("reflection", "Sky reflection", 0.f, 1.f);
    slider("translucency", "Translucency", 0.f, 1.f);
    slider("transparency", "Transparency", 0.f, 1.f);
    slider("normal_strength", "Normal strength", 0.f, 4.f);
    slider("displacement", "Displacement", 0.f, 0.1f);
    // displacement silently does nothing without a height channel or an
    // assignment, so say so instead of leaving the user guessing
    {
      float disp = mat->attrs.get_f("displacement", 0.f);
      bool has_height = a.graph.upstream_node(*mat, "height") != nullptr;
      bool assigned = obj.material_node == mat->id;
      if (disp > 0.f && !has_height) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.45f, 0.2f, 1.f));
        ImGui::TextWrapped("Displacement needs a texture on the 'height' "
                           "channel. Connect one (MaskToTexture from a "
                           "heightmap, or a PBRMaterial height map).");
        ImGui::PopStyleColor();
      }
      if (!assigned) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.45f, 0.2f, 1.f));
        ImGui::TextWrapped("This material is not assigned to %s, so it does "
                           "not affect the viewport.", obj.name.c_str());
        ImGui::PopStyleColor();
      }
    }
    if (changed) {
      a.graph.mark_dirty(mat->id);
      a.request_eval();
      a.uploaded_serial = 0;
    }
  }

  lk.unlock(); // library/AI take the lock themselves
  library_ui(a, obj, mat);
  material_ai_ui(a, obj);
}

static void water_material() {
  RenderSettings &rs = render_settings();
  ImGui::SeparatorText("Body");
  ImGui::ColorEdit3("Deep color", rs.water_deep_color);
  ImGui::ColorEdit3("Shallow color", rs.water_shallow_color);
  ImGui::SliderFloat("Clarity", &rs.water_clarity, 1.f, 60.f);
  ImGui::SliderFloat("Opacity", &rs.water_opacity, 0.3f, 1.f);
  ImGui::SeparatorText("Waves");
  ImGui::SliderFloat("Amplitude", &rs.water_wave_amp, 0.f, 4.f);
  ImGui::SliderFloat("Scale", &rs.water_wave_scale, 0.2f, 6.f);
  ImGui::SliderFloat("Speed", &rs.water_wave_speed, 0.f, 5.f);
  ImGui::SeparatorText("Foam");
  studio::Checkbox("Enabled", &rs.water_foam);
  if (rs.water_foam) {
    ImGui::ColorEdit3("Foam color", rs.foam_color);
    ImGui::SliderFloat("Shoreline", &rs.foam_amount, 0.f, 2.f);
    ImGui::SliderFloat("Crests", &rs.foam_crests, 0.f, 1.f);
    ImGui::SliderFloat("Pattern scale", &rs.foam_scale, 0.5f, 10.f);
  }
}

void material_properties_ui(App &a) {
  SceneState &sc = scene();
  if (sc.selected < 0 || sc.selected >= (int)sc.objects.size()) {
    ImGui::TextDisabled("select an object to edit its material");
    return;
  }
  SceneObject &o = sc.objects[sc.selected];
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.55f, 0.24f, 1.f));
  ImGui::Text("%s", o.name.c_str());
  ImGui::PopStyleColor();
  ImGui::SameLine();
  ImGui::TextDisabled("material");
  ImGui::Separator();

  switch (o.type) {
    case SceneObject::Terrain:
    case SceneObject::Mesh: material_editor(a, o); break;
    case SceneObject::Water: water_material(); break;
    default:
      ImGui::TextDisabled("This object has no surface material.");
      break;
  }
}

} // namespace studio
