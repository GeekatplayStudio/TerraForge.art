// Geekatplay TerraForge — the render editor's state survives a save: the
// backdrop dome, the pass mask, output format and defaults, post-processing,
// and a scene object's binding to the graph node that drives it. Linked into
// undo_tests, which already carries scene_io without a GL context.
#include "render_settings.hpp"
#include "scene.hpp"
#include "scene_io.hpp"
#include <cmath>
#include <cstdio>
#include <string>

namespace render_editor_tests {

static int g_fail = 0, g_checks = 0;
#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    ++g_checks;                                                                \
    if (!(cond)) {                                                             \
      std::printf("  [FAIL] %s (line %d)\n", msg, __LINE__);                   \
      ++g_fail;                                                                \
    }                                                                          \
  } while (0)

static bool near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

static void test_settings_roundtrip() {
  std::printf("render editor settings round-trip...\n");
  studio::RenderSettings &rs = studio::render_settings();
  studio::RenderSettings saved = rs;
  rs.backdrop.enabled = true;
  rs.backdrop.file = "D:/hdri/quarry_4k.exr";
  rs.backdrop.mapping = 4;
  rs.backdrop.vfov = 72.f;
  rs.backdrop.flip = true;
  rs.backdrop.yaw = -35.f;
  rs.backdrop.pitch = 4.f;
  rs.backdrop.exposure_ev = 1.5f;
  rs.backdrop.tint[0] = 0.9f; rs.backdrop.tint[1] = 0.95f; rs.backdrop.tint[2] = 1.1f;
  rs.backdrop.blend = 0.8f;
  rs.backdrop.haze = 0.3f;
  rs.backdrop.hide_sun = false;
  rs.pass_mask = studio::PASS_DEPTH | studio::PASS_ATMOSPHERE | studio::PASS_ENVIRONMENT;
  rs.render_format = 2;
  rs.render_engine = 1;
  rs.render_width = 3840; rs.render_height = 1600; rs.render_samples = 512;
  rs.render_path = "shots/final.exr";
  rs.post_exposure = 1.3f; rs.post_saturation = 0.8f; rs.post_vignette = 0.25f;
  rs.post_tint[0] = 1.05f;

  nlohmann::json j = studio::environment_to_json();
  rs = studio::RenderSettings(); // defaults: everything must come back from the file
  studio::environment_from_json(j, studio::GraphIdMap{});
  CHECK(rs.backdrop.enabled, "backdrop enabled");
  CHECK(rs.backdrop.file == "D:/hdri/quarry_4k.exr", "backdrop file (string field)");
  CHECK(rs.backdrop.mapping == 4, "backdrop mapping");
  CHECK(near(rs.backdrop.vfov, 72.f), "backdrop vfov");
  CHECK(rs.backdrop.flip, "backdrop flip");
  CHECK(near(rs.backdrop.yaw, -35.f) && near(rs.backdrop.pitch, 4.f), "backdrop rotation");
  CHECK(near(rs.backdrop.exposure_ev, 1.5f), "backdrop exposure");
  CHECK(near(rs.backdrop.tint[2], 1.1f), "backdrop tint");
  CHECK(near(rs.backdrop.blend, 0.8f) && near(rs.backdrop.haze, 0.3f), "backdrop blend/haze");
  CHECK(!rs.backdrop.hide_sun, "backdrop sun flag");
  CHECK(rs.pass_mask == (studio::PASS_DEPTH | studio::PASS_ATMOSPHERE | studio::PASS_ENVIRONMENT),
        "pass mask");
  CHECK(rs.render_format == 2 && rs.render_engine == 1, "format and engine");
  CHECK(rs.render_width == 3840 && rs.render_height == 1600 && rs.render_samples == 512,
        "output size and samples");
  CHECK(rs.render_path == "shots/final.exr", "output path (string field)");
  CHECK(near(rs.post_exposure, 1.3f) && near(rs.post_saturation, 0.8f) &&
            near(rs.post_vignette, 0.25f) && near(rs.post_tint[0], 1.05f),
        "post-processing");
  rs = saved;
  // the pass names are file suffixes: short, lower case, unique
  for (int i = 0; i < studio::RENDER_PASS_COUNT; ++i) {
    std::string n = studio::render_pass_name(i);
    CHECK(!n.empty() && n.find(' ') == std::string::npos, "pass name is a file suffix");
    for (int k = 0; k < i; ++k)
      CHECK(n != studio::render_pass_name(k), "pass names are unique");
  }
}

static int test_scene_deform_round_trip() {
  std::printf("  scene deform round-trip\n");
  int fails = 0;
  studio::SceneState &sc = studio::scene();
  sc.objects.clear();
  studio::SceneObject o;
  o.type = studio::SceneObject::Mesh;
  o.name = "twisted";
  o.deform.twist[1] = 45.f;
  o.deform.bend = 20.f;
  o.deform.bend_axis = 2;
  o.deform.shear[0] = 0.25f;
  o.deform.taper = -0.3f;
  o.show_gizmo = false;
  sc.objects.push_back(o);
  nlohmann::json j = studio::scene_to_json();
  sc.objects.clear();
  std::string warnings;
  studio::scene_from_json(j, studio::GraphIdMap{}, warnings);
  if (sc.objects.size() != 1) { std::printf("FAIL: object count\n"); return 1; }
  const studio::SceneObject &b = sc.objects[0];
  auto ck = [&](bool ok, const char *what) { if (!ok) { std::printf("FAIL: %s\n", what); ++fails; } };
  ck(b.deform.twist[1] == 45.f && b.deform.bend == 20.f && b.deform.bend_axis == 2, "twist and bend round-trip");
  ck(b.deform.shear[0] == 0.25f && b.deform.taper == -0.3f, "skew and taper round-trip");
  ck(!b.show_gizmo, "the per-object gizmo switch round-trips");
  // an undeformed object writes no deform keys, so old readers see nothing new
  sc.objects.clear();
  studio::SceneObject plain;
  plain.type = studio::SceneObject::Mesh;
  sc.objects.push_back(plain);
  nlohmann::json j2 = studio::scene_to_json();
  ck(j2.dump().find("twist") == std::string::npos, "an undeformed object writes no deform keys");
  sc.objects.clear();
  return fails;
}

static void test_driver_binding_roundtrip() {
  std::printf("node-driven object binding round-trip...\n");
  studio::SceneState &sc = studio::scene();
  studio::SceneState saved = sc;
  sc = studio::SceneState{};
  studio::scene_init_builtins();
  int li = studio::scene_add_light("Node lamp");
  CHECK(li >= 0, "light created");
  if (li >= 0) sc.objects[li].driver_node = 4242;
  nlohmann::json j = studio::scene_to_json();
  // the id map is how node ids travel across a save: 4242 in the file becomes
  // 7 in the loaded graph, and a binding to an id the map does not know unbinds
  studio::GraphIdMap map;
  map[4242] = 7;
  std::string warnings;
  studio::scene_from_json(j, map, warnings);
  bool found = false;
  for (const studio::SceneObject &o : sc.objects)
    if (o.type == studio::SceneObject::Light && o.name == "Node lamp") {
      found = true;
      CHECK(o.driver_node == 7, "driver node id remapped through the id map");
    }
  CHECK(found, "the light came back");
  studio::scene_from_json(j, studio::GraphIdMap{}, warnings);
  for (const studio::SceneObject &o : sc.objects)
    if (o.type == studio::SceneObject::Light && o.name == "Node lamp")
      CHECK(o.driver_node == 0, "a binding to an unknown node unbinds instead of dangling");
  sc = saved;
}

int run_all() {
  test_settings_roundtrip();
  test_driver_binding_roundtrip();
  std::printf("  render editor persistence: %d checks, %d failures\n", g_checks, g_fail);
  return g_fail;
}

} // namespace render_editor_tests

int test_undo_render_run() { return render_editor_tests::run_all() + render_editor_tests::test_scene_deform_round_trip(); }
