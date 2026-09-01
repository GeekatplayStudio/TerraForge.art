// Geekatplay TerraForge — scene objects & layers
#pragma once
#include "gpx/planet_math.hpp"
#include <string>
#include <vector>

namespace studio {

// A whole procedural planet. Purely parametric — the surface is generated on
// the GPU from these numbers, so any count of planets costs no memory or
// textures. Positions and radii are in world units (1 unit = the home
// terrain tile = terrain_size_m meters).
struct PlanetData {
  float radius = 3.f;
  float relief = 0.02f;      // max relief as a fraction of the radius
  uint32_t seed = 1;
  float sea_level = 0.35f;   // 0..1 within the relief range; <=0 = no ocean
  float snow_line = 0.75f;   // altitude where snow begins; >=1 = none
  float rock_low[3] = {0.38f, 0.34f, 0.30f};
  float rock_high[3] = {0.55f, 0.51f, 0.47f};
  float water_color[3] = {0.06f, 0.16f, 0.28f};
  float atmo_color[3] = {0.45f, 0.62f, 0.90f};
  float atmo_density = 0.6f; // 0 = airless rim
  float spin_deg = 0.f;      // static rotation about Y, for variety
};

// One infinite procedural terrain layer. Parented to a Planet it shapes that
// planet's surface; at the root it extends the home ground plane to the
// horizon. Any number can be stacked — they sum.
struct InfiniteSurfaceData {
  gpx::planet::Layer layer;   // seed, type, frequency, amplitude, coverage
  float height_scale = 1.f;   // extra multiplier for ground-plane layers
};

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
  enum Type { Terrain, Water, Sun, Atmosphere, Mesh, Group, Camera, Planet,
              InfiniteSurface };
  Type type = Mesh;
  PlanetData planet;          // valid when type == Planet
  InfiniteSurfaceData surf;   // valid when type == InfiniteSurface
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
  // Transform. Position is in world units (1 unit = the home terrain tile =
  // terrain_size_m metres); the interface shows every one of these as a real
  // length. Rotation is HPB - heading about Y, then pitch about X, then bank
  // about Z - which is the order Cinema 4D and Vue both use, so a number
  // typed here means what it means there. `scl` squeezes the uniform `scale`
  // per axis, so a rock can be flattened without being resized.
  float pos[3] = {0.5f, 0.05f, 0.5f};
  float scale = 0.08f;
  float scl[3] = {1.f, 1.f, 1.f};
  float yaw = 0.f;   // heading, degrees
  float pitch = 0.f; // degrees
  float roll = 0.f;  // bank, degrees
  float color[3] = {0.62f, 0.60f, 0.57f};
  std::vector<float> verts; // interleaved pos(3) + normal(3)
  unsigned vao = 0, vbo = 0;
  int vert_count = 0;
  bool gpu_dirty = false;
};

// The object's model matrix (column-major, for OpenGL) and the matching
// normal matrix. One definition, used by the renderer, by picking and by the
// selection outline, so a transform can never mean two different things.
void scene_object_matrix(const SceneObject &o, float height_scale, float *m16,
                         float *n9);
// Largest of the three axis scales - the radius picking and outlines use.
float scene_object_radius(const SceneObject &o);

// The tree's per-type glyph and label. Defined next to the panel that draws
// the tree so a new object type is described in exactly one place.
enum class Icon; // studio/icons.hpp
Icon scene_type_icon(SceneObject::Type t);
const char *scene_type_name(SceneObject::Type t);

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

// ---- planets & infinite surfaces ----
// creates a planet at a free spot in space; returns its index
int scene_add_planet(const std::string &name = "");
// creates an infinite terrain layer; parent = planet object index, or -1 for
// the home ground plane. Returns its index.
int scene_add_infinite_surface(int parent = -1, const std::string &name = "");
std::vector<int> scene_planet_indices();
// the infinite layers that apply to `planet_idx` (-1 = home ground plane),
// visible ones only, outliner order
std::vector<int> scene_surface_layers(int planet_idx);

// ---- cameras ----
// creates a camera under the "Cameras" group, inheriting every property
// from the last used camera (or sensible defaults); returns its index
int scene_add_camera(const std::string &name = "");
int scene_cameras_group();               // index of the Cameras group object
std::vector<int> scene_camera_indices(); // all cameras, in outliner order
int &scene_active_camera();              // -1 = free viewport camera
int &scene_last_used_camera();

} // namespace studio
