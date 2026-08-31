#include "i18n.hpp"
#include <map>
#include <string>
#include <vector>

namespace studio {

// ---------------------------------------------------------------- English
// Tags are grouped by area. Keep them stable: translations key off them.
static const std::map<std::string, const char *> &english() {
  static const std::map<std::string, const char *> D = {
      // menus
      {"menu.file", "File"},
      {"menu.file.new", "New"},
      {"menu.file.open", "Open..."},
      {"menu.file.open_recent", "Open Recent"},
      {"menu.file.recent_empty", "(no recent projects)"},
      {"menu.file.clear_recent", "Clear list"},
      {"menu.file.save", "Save"},
      {"menu.file.save_as", "Save As..."},
      {"menu.file.import", "Import"},
      {"menu.file.import_obj", "3D object (OBJ)..."},
      {"menu.file.import_heightmap", "Heightfield image..."},
      {"menu.file.import_material", "PBR texture set..."},
      {"menu.file.export", "Export"},
      {"menu.file.export_heightmap", "Heightmap..."},
      {"menu.file.export_mesh", "Terrain mesh (OBJ)..."},
      {"menu.file.export_texture", "Terrain texture..."},
      {"menu.file.render_image", "Render Image..."},
      {"menu.file.properties", "Project Properties..."},
      {"menu.file.exit", "Exit"},
      {"menu.edit", "Edit"},
      {"menu.edit.undo", "Undo"},
      {"menu.edit.redo", "Redo"},
      {"menu.edit.history", "History"},
      {"menu.edit.delete", "Delete node"},
      {"menu.edit.duplicate", "Duplicate node"},
      {"menu.edit.recompute", "Recompute all"},
      {"menu.edit.preferences", "Preferences..."},
      {"menu.view", "View"},
      {"menu.view.reset_layout", "Reset layout"},
      {"menu.view.viewport_windows", "Viewport windows"},
      {"menu.help", "Help"},
      {"menu.help.about", "About Geekatplay TerraForge..."},
      // workspaces
      {"workspace.terrain", "Terrain"},
      {"workspace.materials", "Materials"},
      {"workspace.atmosphere", "Atmosphere"},
      {"workspace.render", "Render"},
      // panels
      {"panel.outliner", "Outliner"},
      {"panel.properties", "Properties"},
      {"panel.graph", "Graph"},
      {"panel.library", "Library"},
      {"panel.ai", "AI"},
      {"panel.render_output", "Render output"},
      // properties tabs
      {"tab.render", "Render"},
      {"tab.scene", "Scene"},
      {"tab.world", "World"},
      {"tab.object", "Object"},
      {"tab.camera", "Camera"},
      {"tab.material", "Material"},
      {"tab.node", "Node"},
      // common
      {"common.search_properties", "search properties..."},
      {"common.visible", "Visible"},
      {"common.name", "Name"},
      {"common.reset", "reset"},
      {"common.apply", "Apply"},
      {"common.cancel", "Cancel"},
      {"common.close", "Close"},
      {"common.computing", "computing..."},
      {"common.ask_ai", "Ask AI"},
      // camera
      {"camera.lens", "Lens"},
      {"camera.sensor_format", "Sensor format"},
      {"camera.focal_length", "Focal length"},
      {"camera.exposure", "Exposure"},
      {"camera.aperture", "Aperture"},
      {"camera.shutter", "Shutter"},
      {"camera.iso", "ISO"},
      {"camera.film", "Film"},
      {"camera.look_through", "Look through this camera"},
      {"camera.looking_through", "Looking through this camera"},
      {"camera.through_the_lens", "Through the lens"},
      {"camera.add", "+ add camera"},
      // scene / world
      {"world.sun", "Sun"},
      {"world.atmosphere", "Atmosphere"},
      {"world.clouds", "Clouds (volumetric)"},
      {"world.fog", "Fog / Haze"},
      {"world.water", "Water"},
      {"world.planet", "Planet"},
      {"world.planet_radius", "Planet radius"},
      {"world.fractal_detail", "Fractal detail"},
      {"world.fractal_scale", "Detail scale"},
  };
  return D;
}

static int g_language = 0;
static int g_missing = 0;

int i18n_language_count() { return 1; }

const char *i18n_language_name(int index) {
  static const char *names[] = {"English"};
  return names[index == 0 ? 0 : 0];
}

int &i18n_language() { return g_language; }
int i18n_missing_count() { return g_missing; }

const char *tr(const char *tag) {
  const auto &d = english();
  auto it = d.find(tag);
  if (it != d.end()) return it->second;
  ++g_missing; // shows up in Preferences as a translation gap
  return tag;  // visible, never empty
}

} // namespace studio

