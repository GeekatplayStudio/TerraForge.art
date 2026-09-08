// Geekatplay TerraForge — the left tool column: the modes and tools of the
// chosen workflow, stacked down the left edge of the workspace the way
// Cinema 4D's mode palette is.
//
// A mode is something you switch into and then work in - Move, Sculpt with
// the Raise brush, Autokey. A setting is a value you dial - the resolution,
// the sun's height. Modes live here; settings live on the tool row above
// the viewports (toolbar_tools.cpp). Mixing the two on one row was what made
// the old bar read as a wall.
//
// The three transform modes head every workflow, because an object can be
// selected in any of them; the rest of the column changes with the
// workspace. Every button is a palette icon in its functional colour.
#include "anim_widgets.hpp"
#include "app.hpp"
#include "component_new.hpp"
#include "i18n.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "sculpt.hpp"
#include "theme_colors.hpp"
#include "toolbar_internal.hpp"
#include "undo.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <imgui.h>
#include <string>

namespace studio {

// The width of the column: one palette button plus the padding either side.
float left_tools_width() { return tool_size() + 14.f; }

namespace {

void column_terrain(App &a) {
  SculptState &s = sculpt_state();
  if (tool_icon(Icon::Brush, "##sculpt",
                tr("Sculpt\n\nBrush directly on the terrain. Strokes live in a\n"
                   "TerrainSculpt node, so the procedural chain under\n"
                   "them survives retuning."),
                s.active))
    sculpt_set_active(a, !s.active);
  if (s.active) {
    tool_sep();
    struct B {
      Icon icon;
      SculptTool tool;
      const char *id, *tip;
    } brushes[] = {
        {Icon::Raise, SculptTool::Raise, "##b_raise",
         "Raise\n\nAdd relief. Hold Alt (or turn on Invert) to dig."},
        {Icon::Flatten, SculptTool::Flatten, "##b_flat",
         "Flatten\n\nPull the surface toward the height under the first click."},
        {Icon::Smooth, SculptTool::Smooth, "##b_smooth",
         "Smooth\n\nRelax bumps and stroke marks."},
        {Icon::Terrace, SculptTool::Terrace, "##b_terr",
         "Terrace\n\nCut the slope under the brush into steps."},
        {Icon::Noise, SculptTool::Noise, "##b_noise",
         "Noise\n\nStamp fractal detail (Alt inverts)."},
        {Icon::Erase, SculptTool::Erase, "##b_erase",
         "Erase\n\nRemove sculpted strokes, revealing the procedural terrain."}};
    for (const B &b : brushes)
      if (tool_icon(b.icon, b.id, tr(b.tip), s.tool == b.tool)) s.tool = b.tool;
  }
  tool_sep();
  if (tool_icon(Icon::Bake, "##bake4k",
                tr("Bake 4k exports\n\nRe-evaluate at 4096 with every export node enabled."))) {
    std::lock_guard<App::GraphMutex> lk(a.graph_mtx);
    for (auto &n : a.graph.nodes)
      if (auto *e = n->attrs.find("auto_export")) e->b = true;
    a.graph.resolution = 4096;
    a.graph.mark_all_dirty();
    a.request_eval();
    a.status = "baking at 4096; export nodes write when done";
  }
}

// One button for the whole primitive set.
//
// It used to be five, all wearing the cube glyph with a letter in the corner
// - which is a label, not an icon: you had to read it to tell a sphere from
// a cone, and five near-identical tiles is four of them wasted. Now it is one
// tile showing the shape it will make, left-click to make it, right-click to
// pick another, and the corner triangle to say so. Cinema 4D groups its
// object tools exactly this way.
void column_objects(App &a) {
  SceneState &sc = scene();
  gizmo_deform_tools();
  tool_sep();
  struct P {
    const char *kind, *label;
    Icon icon;
  };
  static const P prims[] = {{"cube", "Cube", Icon::Object},
                            {"sphere", "Sphere", Icon::Sphere},
                            {"plane", "Plane", Icon::Plane},
                            {"cylinder", "Cylinder", Icon::Cylinder},
                            {"cone", "Cone", Icon::Cone}};
  static const int PRIM_COUNT = (int)(sizeof prims / sizeof prims[0]);
  // The kind the button makes, remembered for the session the way a grouped
  // tool palette remembers which of its tools you last reached for.
  static int chosen = 0;
  auto add_primitive = [&](const P &p) {
    // The whole component: the object, the node in the editor that drives it,
    // and a material on it - the last one used, or a plain grey one.
    const NewComponent nc = component_add_primitive(a, p.kind);
    if (nc.object >= 0) {
      sc.selected = nc.object;
      a.scene_selection_serial++;
    }
  };
  const P &cur = prims[std::clamp(chosen, 0, PRIM_COUNT - 1)];
  char tip[192];
  std::snprintf(tip, sizeof tip,
                "Add %s\n\nRight-click for the other shapes: cube, sphere,\n"
                "plane, cylinder, cone. The one you pick becomes what this\n"
                "button makes.",
                cur.label);
  if (tool_icon(cur.icon, "##addprim", tip, false, true)) add_primitive(cur);
  if (ImGui::BeginPopupContextItem("##primset")) {
    for (int i = 0; i < PRIM_COUNT; ++i)
      if (IconMenuItem(prims[i].icon, prims[i].label, i == chosen)) {
        chosen = i;
        add_primitive(prims[i]);
      }
    ImGui::EndPopup();
  }
  tool_sep();
  if (tool_icon(Icon::Planet, "##addplanet", tr("om.add_planet_tip"))) {
    undo_push(a, "Add planet");
    sc.selected = scene_add_planet();
    a.scene_selection_serial++;
  }
  if (tool_icon(Icon::Terrain, "##addinf", tr("om.add_infinite_tip"))) {
    undo_push(a, "Add infinite terrain");
    sc.selected = scene_add_infinite_surface(-1);
    a.scene_selection_serial++;
  }
  tool_sep();
  if (tool_icon(Icon::Modifier, "##meshtools",
                tr("Mesh Tools\n\nAnalyse, repair, reduce and export the selected mesh."),
                a.show_mesh_tools))
    a.show_mesh_tools = !a.show_mesh_tools;
}

void column_materials(App &a) {
  RenderSettings &rs = render_settings();
  if (tool_icon(Icon::Material, "##mateditor", tr("Material Editor"), a.show_material_editor))
    a.show_material_editor = !a.show_material_editor;
  if (tool_icon(Icon::Folder, "##matbrowser", tr("Material Browser"), a.show_material_browser))
    a.show_material_browser = !a.show_material_browser;
  tool_sep();
  if (tool_icon(Icon::Textured, "##textured",
                tr("Textured terrain\n\nShow the material's colour on the terrain\n"
                   "instead of the height shading."),
                rs.use_albedo))
    rs.use_albedo = !rs.use_albedo;
}

void column_atmosphere(App &a) {
  (void)a;
  RenderSettings &rs = render_settings();
  if (tool_icon(Icon::Cloud, "##clouds", tr("Clouds"), rs.clouds_on)) rs.clouds_on = !rs.clouds_on;
  if (tool_icon(Icon::Water, "##water", tr("Water"), rs.show_water)) rs.show_water = !rs.show_water;
  if (tool_icon(Icon::Sun, "##shadows", tr("Shadows"), rs.shadows)) rs.shadows = !rs.shadows;
}

void column_lighting(App &a) {
  RenderSettings &rs = render_settings();
  if (tool_icon(Icon::Sun, "##shadows2", tr("Shadows"), rs.shadows)) rs.shadows = !rs.shadows;
  tool_sep();
  if (tool_icon(Icon::Light, "##addlight",
                tr("Add light\n\nA point light in the scene. A LightSource node in the\n"
                   "graph does the same and keeps it in the network."))) {
    undo_push(a, "Add light");
    scene().selected = scene_add_light("");
    a.scene_selection_serial++;
  }
}

void column_cameras(App &a) {
  SceneState &sc = scene();
  if (tool_icon(Icon::Camera, "##addcam", tr("Add camera"))) {
    int idx = scene_add_camera();
    scene_active_camera() = idx;
    sc.selected = idx;
    a.scene_selection_serial++;
  }
  // look through the selected camera, or back out to the free camera
  const bool sel_cam = sc.selected >= 0 && sc.selected < (int)sc.objects.size() &&
                       sc.objects[sc.selected].type == SceneObject::Camera;
  const bool through = sel_cam && scene_active_camera() == sc.selected;
  if (tool_icon(Icon::Eye, "##lookthrough",
                tr("Look through the selected camera\n\nAgain to return to the free camera."),
                through) && sel_cam) {
    scene_active_camera() = through ? -1 : sc.selected;
    if (!through) scene_last_used_camera() = sc.selected;
  }
}

void column_animation(App &a) {
  gpx::Timeline &tl = scene().timeline;
  if (tool_icon(Icon::KeyAdd, "##ak", tr("Key the selected object's transform (K)")))
    anim_key_selection_transform(a);
  if (tool_icon(Icon::Autokey, "##autokey", tr("Autokey"), tl.autokey)) tl.autokey = !tl.autokey;
  tool_sep();
  if (tool_icon(Icon::Timeline, "##timeline", tr("Timeline"), a.show_timeline))
    a.show_timeline = !a.show_timeline;
  if (tool_icon(Icon::Curve, "##curves", tr("Curves"), a.show_curve_editor))
    a.show_curve_editor = !a.show_curve_editor;
}

void column_render(App &a) {
  if (tool_icon(Icon::Scene, "##previewpanel", tr("Preview render panel"), a.show_preview))
    a.show_preview = !a.show_preview;
  if (tool_icon(Icon::Render, "##rendercam2",
                tr("Render the active camera\n\nRender through the active camera with its own\n"
                   "engine, resolution and sample settings.")))
    a.request_camera_render = scene_active_camera();
}

} // namespace

void draw_left_tools(App &a) {
  tool_column_begin();
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 3.f));
  tool_pad(6.f);
  gizmo_transform_tools();
  tool_sep();
  gizmo_space_tools();
  tool_sep();
  switch (a.workspace) {
    case WS_MATERIALS: column_materials(a); break;
    case WS_ATMOSPHERE: column_atmosphere(a); break;
    case WS_RENDER: column_render(a); break;
    case WS_OBJECTS: column_objects(a); break;
    case WS_LIGHTING: column_lighting(a); break;
    case WS_CAMERAS: column_cameras(a); break;
    case WS_ANIMATION: column_animation(a); break;
    default: column_terrain(a); break;
  }
  tool_sep();
  gizmo_visible_tool();
  if (tool_icon(Icon::Console, "##console", tr("Show the console"), a.show_console))
    a.show_console = !a.show_console;
  ImGui::PopStyleVar();
  tool_column_end();
}

} // namespace studio
