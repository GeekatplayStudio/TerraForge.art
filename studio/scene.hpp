// Geekatplay TerraForge — scene objects & layers
#pragma once
#include <string>
#include <vector>

namespace studio {

struct SceneObject {
  enum Type { Terrain, Water, Sun, Atmosphere, Mesh };
  Type type = Mesh;
  std::string name;
  int layer = 0;
  bool visible = true;
  bool builtin = false;
  // imported mesh data
  std::string path;
  unsigned long long material_node = 0; // MaterialOutput node driving this object
  float pos[3] = {0.5f, 0.05f, 0.5f};
  float scale = 0.08f;
  float yaw = 0.f;
  float color[3] = {0.62f, 0.60f, 0.57f};
  std::vector<float> verts; // interleaved pos(3) + normal(3)
  unsigned vao = 0, vbo = 0;
  int vert_count = 0;
  bool gpu_dirty = false;
};

struct SceneLayer {
  std::string name;
  bool visible = true;
};

struct SceneState {
  std::vector<SceneObject> objects;
  std::vector<SceneLayer> layers{{"Default", true}};
  int selected = 0;

  bool object_visible(const SceneObject &o) const {
    bool lv = o.layer >= 0 && o.layer < (int)layers.size()
                  ? layers[o.layer].visible
                  : true;
    return o.visible && lv;
  }
};

SceneState &scene();
void scene_init_builtins();
// load OBJ into a new scene object; returns index or -1
int scene_import_obj(const std::string &path, std::string &err);

} // namespace studio
