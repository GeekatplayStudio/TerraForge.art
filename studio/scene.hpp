// Geekatplay TerraForge — scene objects & layers
#pragma once
#include <string>
#include <vector>

namespace studio {

// per-camera offline render assignment
struct RenderAssign {
  int engine = 0; // index into the Render tab's engine list
  int width = 1920, height = 1080;
  int samples = 128;
  std::string output = "render.png";
};

struct CameraData {
  float eye[3] = {0.5f, 0.45f, 1.7f};
  float target[3] = {0.5f, 0.1f, 0.5f};
  // physical lens
  float focal_mm = 35.f;
  int format = 0;         // index into gpx::cam::sensor_formats
  float aperture = 8.f;   // f-number
  float shutter = 1.f / 125.f;
  float iso = 100.f;
  int film = 0;           // index into gpx::cam::film_stocks
  RenderAssign render;
};

struct SceneObject {
  enum Type { Terrain, Water, Sun, Atmosphere, Mesh, Group, Camera };
  Type type = Mesh;
  std::string name;
  int layer = 0;
  int parent = -1;       // index into objects, -1 = root
  bool expanded = true;  // groups: children shown in the Outliner
  bool visible = true;
  bool builtin = false;
  CameraData cam;        // valid when type == Camera
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

// ---- cameras ----
// creates a camera under the "Cameras" group, inheriting every property
// from the last used camera (or sensible defaults); returns its index
int scene_add_camera(const std::string &name = "");
int scene_cameras_group();               // index of the Cameras group object
std::vector<int> scene_camera_indices(); // all cameras, in outliner order
int &scene_active_camera();              // -1 = free viewport camera
int &scene_last_used_camera();

} // namespace studio
