// Geekatplay TerraForge - animation on scene objects and the world: the
// property table resolves every path it declares, recording a key reads
// the live value, applying at a time writes it back, removing the last key
// makes the property static again, tracks and the timeline survive the
// scene file, and the expression lookup reaches an object by name.
// Linked into undo_tests (it needs the scene and its serializers).
#include "anim_targets.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "scene_io.hpp"
#include <cmath>
#include <cstdio>
#include <string>

using namespace studio;

static int g_fail = 0;
#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("  [FAIL] %s (line %d)\n", msg, __LINE__);                   \
      g_fail++;                                                                \
    }                                                                          \
  } while (0)

static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

static void test_property_table() {
  std::printf("animation: property table...\n");
  SceneObject o;
  o.type = SceneObject::Mesh;
  int n = 0;
  for (const AnimProp *p : anim_props_for(o)) {
    if (p->boolean) { CHECK(anim_bool_ptr(o, *p) != nullptr, (std::string("bool path resolves: ") + p->path).c_str()); continue; }
    for (int c = 0; c < p->comps; ++c) CHECK(anim_ptr(o, *p, c) != nullptr, (std::string("path resolves: ") + p->path).c_str());
    ++n;
  }
  CHECK(n >= 8, "a mesh has transform, colour, deform and scatter properties");
  CHECK(anim_find_prop(o, "light.intensity") == nullptr, "a mesh has no light properties");
  SceneObject l;
  l.type = SceneObject::Light;
  CHECK(anim_find_prop(l, "light.intensity") != nullptr && anim_ptr(l, *anim_find_prop(l, "light.intensity"), 0) == &l.light_intensity, "a light's intensity resolves to the field");
  SceneObject cam;
  cam.type = SceneObject::Camera;
  CHECK(anim_ptr(cam, *anim_find_prop(cam, "cam.focal_mm"), 0) == &cam.cam.focal_mm, "camera focal length resolves");
  RenderSettings rs;
  for (const AnimProp &p : anim_world_props())
    for (int c = 0; c < p.comps; ++c) CHECK(anim_world_ptr(rs, p, c) != nullptr, (std::string("world path resolves: ") + p.path).c_str());
  CHECK(anim_key(*anim_find_prop(o, "pos"), 2) == "pos.z" && anim_key(*anim_find_prop(o, "color"), 0) == "color.r", "component keys");
}

static void test_record_apply() {
  std::printf("animation: record, apply, unkey...\n");
  SceneState &sc = scene();
  sc = SceneState{};
  SceneObject o;
  o.type = SceneObject::Mesh;
  o.name = "Rock";
  o.pos[0] = 0.2f;
  sc.objects.push_back(o);
  SceneObject &r = sc.objects[0];
  const AnimProp *pos = anim_find_prop(r, "pos");
  CHECK(!anim_object_animated(r), "static until keyed");
  CHECK(anim_record(r, *pos, -1, 0.f), "first key on every component");
  CHECK(r.anim.size() == 3 && anim_prop_keyed_at(r.anim, *pos, 0.f), "three tracks, keyed at 0");
  r.pos[0] = 0.8f;
  anim_record(r, *pos, 0, 2.f);
  CHECK(r.anim["pos.x"].keys.size() == 2 && r.anim["pos.y"].keys.size() == 1, "a single component keys alone");
  RenderSettings rs;
  bool sch = false, wch = false;
  anim_apply(sc, rs, 1.f, sch, wch);
  CHECK(sch && !wch, "applying reports the scene changed");
  CHECK(r.pos[0] > 0.2f && r.pos[0] < 0.8f, "halfway between the keys");
  anim_apply(sc, rs, 5.f, sch, wch);
  CHECK(near(r.pos[0], 0.8f), "after the last key holds");
  // world
  const AnimProp *az = anim_find_world_prop("sun_azimuth");
  rs.sun_azimuth = 90.f;
  anim_record_world(rs, *az, -1, 0.f);
  rs.sun_azimuth = 180.f;
  anim_record_world(rs, *az, -1, 1.f);
  anim_apply(sc, rs, 0.5f, sch, wch);
  CHECK(wch && rs.sun_azimuth > 90.f && rs.sun_azimuth < 180.f, "world properties animate too");
  // unkey to static
  anim_unkey(r.anim, *pos, 0, 2.f);
  anim_unkey(r.anim, *pos, -1, 0.f);
  CHECK(r.anim.empty(), "removing the last key drops the track: static again");
  r.pos[0] = 0.33f;
  anim_apply(sc, rs, 1.f, sch, wch);
  CHECK(near(r.pos[0], 0.33f), "a static property is left alone");
  // bool
  const AnimProp *vis = anim_find_prop(r, "visible");
  r.visible = true; anim_record(r, *vis, -1, 0.f);
  r.visible = false; anim_record(r, *vis, -1, 1.f);
  anim_apply(sc, rs, 0.4f, sch, wch);
  CHECK(r.visible == true, "a bool track holds (step) before the next key");
  anim_apply(sc, rs, 1.f, sch, wch);
  CHECK(r.visible == false, "and switches at the key");
  // expression lookup by name
  float v;
  r.pos[2] = 0.77f;
  CHECK(anim_lookup("Rock.pos.z", v) && near(v, 0.77f), "lookup by object name and path");
  CHECK(anim_lookup("world.sun_azimuth", v), "lookup a world property");
  CHECK(!anim_lookup("Nobody.pos.x", v) && !anim_lookup("Rock.nothing", v), "unknown names fail");
  sc = SceneState{};
}

static void test_scene_file() {
  std::printf("animation: scene file round trip...\n");
  SceneState &sc = scene();
  sc = SceneState{};
  SceneObject o;
  o.type = SceneObject::Mesh;
  o.name = "Rock";
  sc.objects.push_back(o);
  SceneObject &r = sc.objects[0];
  const AnimProp *pos = anim_find_prop(r, "pos");
  anim_record(r, *pos, -1, 0.f);
  r.pos[1] = 0.5f;
  anim_record(r, *pos, 1, 1.5f);
  r.anim["pos.y"].post = gpx::Extrapolate::Cycle;
  r.anim["pos.y"].expr = "value * 2";
  sc.timeline.fps = 25.f;
  sc.timeline.end = 4.f;
  sc.timeline.autokey = true;
  sc.timeline.markers.push_back({1.f, "hit"});
  RenderSettings rs;
  anim_record_world(rs, *anim_find_world_prop("fog_density"), -1, 0.f);
  nlohmann::json j = scene_to_json();
  sc = SceneState{};
  std::string warn;
  scene_from_json(j, GraphIdMap{}, warn);
  CHECK(sc.objects.size() == 1 && sc.objects[0].anim.count("pos.y") == 1, "object tracks come back");
  CHECK(sc.objects[0].anim["pos.y"].keys.size() == 2 && sc.objects[0].anim["pos.y"].post == gpx::Extrapolate::Cycle, "keys and extrapolation come back");
  CHECK(sc.objects[0].anim["pos.y"].expr == "value * 2", "expressions come back");
  CHECK(near(sc.timeline.fps, 25.f) && near(sc.timeline.end, 4.f) && sc.timeline.autokey, "timeline settings come back");
  CHECK(sc.timeline.markers.size() == 1 && sc.timeline.markers[0].name == "hit", "markers come back");
  CHECK(sc.world_anim.count("fog_density") == 1, "world tracks come back");
  // an old file without any of this loads to defaults
  nlohmann::json old = j;
  old.erase("timeline");
  old.erase("world_anim");
  old["objects"][0].erase("anim");
  sc = SceneState{};
  scene_from_json(old, GraphIdMap{}, warn);
  CHECK(sc.objects[0].anim.empty() && sc.world_anim.empty() && near(sc.timeline.fps, 30.f), "an old file is static at 30 fps");
  sc = SceneState{};
}

int test_anim_scene_run() {
  test_property_table();
  test_record_apply();
  test_scene_file();
  return g_fail;
}
