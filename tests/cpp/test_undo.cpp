// Geekatplay TerraForge â€” undo / redo test suite.
//
// Undo is snapshot-based, so these tests check the two things that can go
// wrong with that design: that a restored state really equals the state we
// left, and that the position in the history stack stays consistent through
// branching, truncation and overflow.
#include "app.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "node_library.hpp"
#include "undo.hpp"
#include "gpx/metanode.hpp"
#include "gpx/node_graph.hpp"
#include "gpx/serialization.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

static int g_failures = 0;
#define CHECK(cond, msg)                                                        \
  do {                                                                          \
    if (!(cond)) {                                                              \
      std::printf("  [FAIL] %s (line %d)\n", msg, __LINE__);                    \
      g_failures++;                                                             \
    }                                                                           \
  } while (0)

namespace studio {
// The real definition lives in renderer.cpp, which needs a GL context. The
// settings themselves are plain data, so the test provides its own instance.
RenderSettings &render_settings() {
  static RenderSettings rs;
  return rs;
}
} // namespace studio

using namespace studio;

// A fresh app + scene for each test, so tests cannot leak into each other.
static void reset_all(App &a) {
  undo_clear();
  a.graph.clear();
  a.graph.resolution = 64;
  a.selected_node = a.view_node = 0;
  scene() = SceneState{};
  scene_active_camera() = -1;
  scene_last_used_camera() = -1;
  render_settings() = RenderSettings{};
  scene_init_builtins();
}

static int node_count(App &a) { return (int)a.graph.nodes.size(); }

static void test_graph_undo() {
  std::printf("undo: node graph...\n");
  App a;
  reset_all(a);
  a.graph.add_node("Noise", 10, 20);
  int before = node_count(a);

  undo_push(a, "Add Smooth");
  a.graph.add_node("Smooth", 100, 20);
  CHECK(node_count(a) == before + 1, "node added");

  CHECK(undo_can_undo(), "undo available after an edit");
  CHECK(undo_next_label() == "Add Smooth", "undo names the step it reverts");
  undo_perform(a);
  CHECK(node_count(a) == before, "undo removed the added node");
  CHECK(a.graph.nodes[0]->type == "Noise", "the original node survived");

  CHECK(undo_can_redo(), "redo available after an undo");
  CHECK(undo_redo_label() == "Add Smooth", "redo names the step it reapplies");
  redo_perform(a);
  CHECK(node_count(a) == before + 1, "redo restored the added node");
  CHECK(a.graph.nodes[1]->type == "Smooth", "redo restored the right type");
}

static void test_attributes_and_links() {
  std::printf("undo: attributes and links...\n");
  App a;
  reset_all(a);
  gpx::Node *noise = a.graph.add_node("Noise");
  gpx::Node *smooth = a.graph.add_node("Smooth");
  a.graph.add_link(noise->id, "output", smooth->id, "input");
  CHECK(a.graph.links.size() == 1, "link created");

  gpx::Attribute *seed = noise->attrs.find("seed");
  CHECK(seed != nullptr, "the Noise node has a seed attribute");
  uint32_t original_seed = seed ? seed->seed : 0;

  undo_push(a, "Edit Noise");
  // change an attribute and drop the link
  if (seed) seed->seed = 4242;
  a.graph.remove_link(a.graph.links[0].id);
  CHECK(a.graph.links.empty(), "link removed");

  undo_perform(a);
  CHECK(a.graph.links.size() == 1, "undo restored the link");
  gpx::Attribute *after_undo = a.graph.nodes[0]->attrs.find("seed");
  CHECK(after_undo && after_undo->seed == original_seed,
        "undo restored the attribute value");

  redo_perform(a);
  CHECK(a.graph.links.empty(), "redo re-removed the link");
  gpx::Attribute *after_redo = a.graph.nodes[0]->attrs.find("seed");
  CHECK(after_redo && after_redo->seed == 4242,
        "redo re-applied the attribute value");
}

static void test_world_undo() {
  std::printf("undo: world settings...\n");
  App a;
  reset_all(a);
  render_settings().sun_azimuth = 120.f;
  render_settings().fog_density = 1.f;

  undo_push(a, "Set sun");
  render_settings().sun_azimuth = 300.f;
  render_settings().fog_density = 4.f;

  undo_perform(a);
  CHECK(std::fabs(render_settings().sun_azimuth - 120.f) < 1e-4f,
        "undo restored the sun azimuth");
  CHECK(std::fabs(render_settings().fog_density - 1.f) < 1e-4f,
        "undo restored the fog density");
  redo_perform(a);
  CHECK(std::fabs(render_settings().sun_azimuth - 300.f) < 1e-4f,
        "redo re-applied the sun azimuth");
}

static void test_scene_undo() {
  std::printf("undo: scene objects...\n");
  App a;
  reset_all(a);
  int before = (int)scene().objects.size();

  undo_push(a, "Add camera");
  int cam = scene_add_camera("Test Cam");
  scene_active_camera() = cam;
  CHECK((int)scene().objects.size() > before, "camera added");

  undo_perform(a);
  CHECK((int)scene().objects.size() == before, "undo removed the camera");
  CHECK(scene_active_camera() == -1, "undo restored the active camera");

  redo_perform(a);
  CHECK((int)scene().objects.size() > before, "redo restored the camera");
  CHECK(scene_active_camera() == cam, "redo restored the active camera");
  CHECK(scene().objects[cam].name == "Test Cam", "redo kept the camera name");
}

static void test_mesh_vertices_survive() {
  std::printf("undo: imported mesh data...\n");
  App a;
  reset_all(a);
  // stand in for an imported mesh: snapshots share vertex data rather than
  // copying it, so this checks the sharing still round trips
  SceneObject m;
  m.type = SceneObject::Mesh;
  m.name = "Rock";
  m.path = "rock.obj";
  m.verts.assign(600, 0.f);
  for (size_t i = 0; i < m.verts.size(); ++i) m.verts[i] = (float)i * 0.5f;
  m.vert_count = 100;
  scene().objects.push_back(m);

  undo_push(a, "Delete object");
  scene().objects.pop_back();
  CHECK(scene().objects.empty() || scene().objects.back().name != "Rock",
        "mesh removed");

  undo_perform(a);
  CHECK(!scene().objects.empty() && scene().objects.back().name == "Rock",
        "undo restored the mesh object");
  const SceneObject &r = scene().objects.back();
  CHECK(r.verts.size() == 600, "undo restored the vertex buffer");
  bool same = r.verts.size() == 600;
  for (size_t i = 0; same && i < r.verts.size(); ++i)
    if (r.verts[i] != (float)i * 0.5f) same = false;
  CHECK(same, "vertex data is bit-identical after undo");
}

static void test_redo_branch_truncation() {
  std::printf("undo: redo branch truncation...\n");
  App a;
  reset_all(a);
  undo_push(a, "Step A");
  a.graph.add_node("Noise");
  undo_push(a, "Step B");
  a.graph.add_node("Smooth");
  CHECK(node_count(a) == 2, "two nodes");

  undo_perform(a); // back to one node, redo available
  CHECK(undo_can_redo(), "redo available");

  undo_push(a, "Step C"); // a new edit must discard the redo branch
  a.graph.add_node("Terrace");
  CHECK(!undo_can_redo(), "new edit discarded the redo branch");
  CHECK(node_count(a) == 2, "the new edit applied");
  CHECK(a.graph.nodes[1]->type == "Terrace", "the branch really changed");

  undo_perform(a);
  CHECK(node_count(a) == 1, "undo of the new branch works");
}

static void test_history_and_jump() {
  std::printf("undo: history list and jump...\n");
  App a;
  reset_all(a);
  undo_push(a, "Add one");
  a.graph.add_node("Noise");
  undo_push(a, "Add two");
  a.graph.add_node("Smooth");
  undo_push(a, "Add three");
  a.graph.add_node("Terrace");

  const std::string *labels = nullptr;
  int n = undo_history(&labels);
  CHECK(n == 4, "history holds the initial state plus three steps");
  CHECK(labels[0] == "Initial state", "the first entry is the initial state");
  CHECK(labels[3] == "Add three", "the last entry is the newest step");
  CHECK(undo_history_position() == 3, "position is at the newest step");

  undo_jump_to(a, 1); // back to just after "Add one"
  CHECK(node_count(a) == 1, "jump restored the state of that step");
  CHECK(undo_history_position() == 1, "position followed the jump");
  CHECK(undo_can_redo(), "steps after the jump are still redoable");

  undo_jump_to(a, 3);
  CHECK(node_count(a) == 3, "jumping forward restores the later state");
}

static void test_stack_overflow_is_stable() {
  std::printf("undo: history cap...\n");
  App a;
  reset_all(a);
  // more steps than the cap: the oldest are dropped, the position stays valid
  for (int i = 0; i < 90; ++i) {
    undo_push(a, "Step " + std::to_string(i));
    a.graph.add_node("Noise");
  }
  const std::string *labels = nullptr;
  int n = undo_history(&labels);
  CHECK(n <= 64, "history is capped");
  CHECK(undo_history_position() == n - 1, "position is at the newest step");
  CHECK(undo_history_position() >= 0, "position stayed valid after trimming");
  int before = node_count(a);
  CHECK(undo_perform(a), "undo still works after trimming");
  CHECK(node_count(a) == before - 1, "undo after trimming restores correctly");
}

// The important guarantee for a procedural tool: undo must not perturb the
// computed result. Round tripping through the history has to give a terrain
// that is bit-identical to the one we started with.
static void test_undo_is_deterministic() {
  std::printf("undo: determinism of the restored graph...\n");
  App a;
  reset_all(a);
  a.graph.resolution = 64;
  gpx::Node *noise = a.graph.add_node("Noise");
  gpx::Node *ero = a.graph.add_node("Hydraulic");
  a.graph.add_link(noise->id, "output", ero->id, "input");
  a.graph.evaluate();

  gpx::Port *p = ero->port("output");
  CHECK(p && p->hmap, "erosion produced output");
  std::vector<float> reference = p->hmap->v;
  CHECK(!reference.empty(), "reference terrain computed");

  undo_push(a, "Add Terrace");
  gpx::Node *ter = a.graph.add_node("Terrace");
  a.graph.add_link(ero->id, "output", ter->id, "input");
  a.graph.evaluate();

  undo_perform(a); // back to noise -> erosion
  a.graph.mark_all_dirty();
  a.graph.evaluate();

  gpx::Node *ero2 = nullptr;
  for (auto &n : a.graph.nodes)
    if (n->type == "Hydraulic") ero2 = n.get();
  CHECK(ero2 != nullptr, "erosion node restored");
  gpx::Port *p2 = ero2 ? ero2->port("output") : nullptr;
  CHECK(p2 && p2->hmap, "restored graph recomputed");
  bool identical = p2 && p2->hmap && p2->hmap->v.size() == reference.size();
  if (identical)
    for (size_t i = 0; i < reference.size(); ++i)
      if (p2->hmap->v[i] != reference[i]) {
        identical = false;
        break;
      }
  CHECK(identical, "terrain after undo is bit-identical to before the edit");
}

static void test_planets_in_scene() {
  std::printf("undo: planets & infinite terrains...\n");
  App a;
  reset_all(a);
  int before = (int)scene().objects.size();

  undo_push(a, "Add planet");
  int p = scene_add_planet("Vulcan");
  CHECK(p >= 0 && scene().objects[p].type == SceneObject::Planet, "planet added");
  // a new planet comes with one surface layer so it is not a smooth ball
  CHECK((int)scene_surface_layers(p).size() == 1, "planet has a starter layer");
  scene().objects[p].planet.radius = 7.5f;
  scene().objects[p].planet.sea_level = 0.f;

  // several planets coexist â€” the multiple-planets promise
  undo_push(a, "Add planet");
  int p2 = scene_add_planet();
  CHECK(p2 != p, "second planet added");
  CHECK((int)scene_planet_indices().size() == 2, "two planets in the scene");
  float dx = scene().objects[p].pos[0] - scene().objects[p2].pos[0];
  float dz = scene().objects[p].pos[2] - scene().objects[p2].pos[2];
  CHECK(dx * dx + dz * dz > 4.f, "auto-placement keeps planets apart");

  // Ground-plane infinite terrain is separate from planet layers. Counted
  // relative to what was already there: a new scene now ships with a planet
  // surface of its own, and the claim being tested is that adding a layer
  // registers one *more*, not that only one can exist.
  const int ground_before = (int)scene_surface_layers(-1).size();
  undo_push(a, "Add infinite terrain");
  int gsurf = scene_add_infinite_surface(-1);
  CHECK((int)scene_surface_layers(-1).size() == ground_before + 1,
        "ground layer registered");
  CHECK((int)scene_surface_layers(p).size() == 1,
        "planet layers unaffected by ground layers");
  (void)gsurf;

  // undo unwinds the whole stack back to nothing
  undo_perform(a);
  CHECK((int)scene_surface_layers(-1).size() == ground_before,
        "undo removed the ground layer");
  undo_perform(a);
  undo_perform(a);
  CHECK((int)scene().objects.size() == before, "undo removed both planets");
  redo_perform(a);
  CHECK((int)scene_planet_indices().size() == 1, "redo restored the planet");
  CHECK(std::fabs(scene()
                      .objects[scene_planet_indices()[0]]
                      .planet.radius -
                  7.5f) < 1e-5f,
        "redo restored the edited radius");
}

static void test_planet_persistence() {
  std::printf("planets survive save/load...\n");
  App a;
  reset_all(a);
  a.graph.add_node("Noise");
  int p = scene_add_planet("Kepler");
  scene().objects[p].planet.radius = 12.25f;
  scene().objects[p].planet.seed = 424242;
  scene().objects[p].planet.sea_level = 0.71f;
  scene().objects[p].pos[0] = -33.f;
  scene_add_infinite_surface(p, "Craggy belt");
  scene_add_infinite_surface(-1, "Home horizon");
  int planets_before = (int)scene_planet_indices().size();
  int psurf_before = (int)scene_surface_layers(p).size();
  int gsurf_before = (int)scene_surface_layers(-1).size();

  const char *path = "test_planets_roundtrip.gpxt";
  CHECK(project_save(a, path), "project saved");

  // wipe and reload
  reset_all(a);
  CHECK(scene_planet_indices().empty(), "scene cleared");
  CHECK(project_load(a, path), "project loaded");
  std::remove(path);

  std::vector<int> planets = scene_planet_indices();
  CHECK((int)planets.size() == planets_before, "planet count restored");
  if (!planets.empty()) {
    const SceneObject &o = scene().objects[planets[0]];
    CHECK(o.name == "Kepler", "planet name restored");
    CHECK(std::fabs(o.planet.radius - 12.25f) < 1e-5f, "radius restored");
    CHECK(o.planet.seed == 424242, "seed restored");
    CHECK(std::fabs(o.planet.sea_level - 0.71f) < 1e-5f, "sea level restored");
    CHECK(std::fabs(o.pos[0] + 33.f) < 1e-5f, "position restored");
    CHECK((int)scene_surface_layers(planets[0]).size() == psurf_before,
          "planet surface layers restored and re-parented");
  }
  CHECK((int)scene_surface_layers(-1).size() == gsurf_before,
        "ground layers restored");
}

// A saved MetaNode has to come back as the same node in a different project,
// with its inner graph, its exposed parameters and their tuned values intact.
// That is what makes it a building block rather than a snapshot.
static void test_node_library_roundtrip() {
  std::printf("MetaNode library round trip...\n");
  App a;
  reset_all(a);
  a.graph.resolution = 48;
  gpx::Node *src = a.graph.add_node("Noise", 0, 0);
  gpx::Node *warp = a.graph.add_node("WarpNoise", 200, 0);
  gpx::Node *ter = a.graph.add_node("Terrace", 400, 0);
  gpx::Node *sink = a.graph.add_node("Thru", 600, 0);
  a.graph.add_link(src->id, "output", warp->id, "input");
  a.graph.add_link(warp->id, "output", ter->id, "input");
  a.graph.add_link(ter->id, "output", sink->id, "input");
  uint64_t inner_ter = ter->id;

  std::string err;
  gpx::Node *meta = gpx::metanode_group(a.graph, {warp->id, inner_ter}, err);
  CHECK(meta != nullptr, "grouped for saving: " + err);
  if (!meta) return;
  CHECK(gpx::metanode_publish(*meta, inner_ter, "levels", "Steps"),
        "published a parameter before saving");
  // tune it so the saved node remembers how it was set, not just its shape
  gpx::Attribute *mirror =
      meta->attrs.find("pub_" + std::to_string(inner_ter) + "_levels");
  CHECK(mirror != nullptr, "mirror attribute present");
  if (!mirror) return;
  mirror->i = 5;
  int saved_ports = (int)meta->ports.size();

  const std::string name = "test_roundtrip_node";
  CHECK(node_library_save(a, meta->id, name, "a test group", err),
        "saved to the library: " + err);

  // find it in a fresh scan, the way the browser would
  std::string path;
  for (const SavedMetaNode &m : node_library(true))
    if (m.name == name) path = m.path;
  CHECK(!path.empty(), "the saved node appears in the library");
  if (path.empty()) return;
  for (const SavedMetaNode &m : node_library())
    if (m.name == name) {
      CHECK(m.note == "a test group", "the note is kept");
      CHECK(m.inner_nodes == 2, "the library reports what is inside");
      CHECK(m.published == 1, "and how many parameters it exposes");
    }

  // load into a brand new project, as a different user would
  App b;
  reset_all(b);
  b.graph.resolution = 48;
  gpx::Node *src2 = b.graph.add_node("Noise", 0, 0);
  unsigned long long id = node_library_load(b, path, 300, 0, err);
  CHECK(id != 0, "loaded into a fresh project: " + err);
  gpx::Node *meta2 = b.graph.find_node(id);
  CHECK(meta2 != nullptr, "the loaded node exists");
  if (!meta2) return;
  CHECK((int)meta2->ports.size() == saved_ports,
        "the boundary ports were rebuilt");
  CHECK(gpx::metanode_published(*meta2).size() == 1,
        "the exposed parameter came back");
  gpx::Attribute *m2 =
      meta2->attrs.find("pub_" + std::to_string(inner_ter) + "_levels");
  CHECK(m2 != nullptr, "the exposed widget came back");
  if (m2) {
    CHECK(m2->i == 5, "and remembers the value it was saved at");
    CHECK(m2->label == "Steps", "and its published label");
  }

  // and it actually computes when wired up in the new project
  bool wired = false;
  for (const gpx::Port &p : meta2->ports)
    if (p.dir == gpx::PortDir::In && p.type == gpx::DataType::Heightmap)
      wired = b.graph.add_link(src2->id, "output", meta2->id, p.name);
  CHECK(wired, "the loaded node can be wired up");
  b.graph.mark_all_dirty();
  b.graph.evaluate();
  CHECK(meta2->error.empty(),
        "the loaded node evaluates without error: " + meta2->error);
  bool produced = false;
  for (const gpx::Port &p : meta2->ports)
    if (p.dir == gpx::PortDir::Out && p.hmap && !p.hmap->empty()) produced = true;
  CHECK(produced, "and produces terrain");

  node_library_delete(path);
  bool gone = true;
  for (const SavedMetaNode &m : node_library(true))
    if (m.name == name) gone = false;
  CHECK(gone, "removing from the library works");
}

// A corrupted node-editor view file made the application spin for ever behind
// a black window on every launch, permanently. The values below are taken from
// the file that actually did it, so this is a regression test in the literal
// sense rather than a hypothetical.
static void test_graph_view_sanity() {
  std::printf("graph view state sanity...\n");

  // healthy files must be left alone, or the check would throw away everyone's
  // pan and zoom on every start
  CHECK(studio::graph_view_is_sane(""), "no saved view is fine");
  CHECK(studio::graph_view_is_sane(
            R"({"nodes":{"node:1":{"location":{"x":260,"y":100}}},)"
            R"("view":{"zoom":1.0612}})"),
        "an ordinary saved view is kept");
  CHECK(studio::graph_view_is_sane(R"({"view":{"zoom":0.25}})"),
        "a zoomed-out but usable view is kept");
  CHECK(studio::graph_view_is_sane(R"({"view":{"zoom":4.0}})"),
        "a zoomed-in view is kept");

  // the real one: zoom collapsed to 4.6e-07 and positions at INT_MIN
  CHECK(!studio::graph_view_is_sane(
            R"({"nodes":{"node:4":{"location":{"x":-2147483648,"y":-2147483648}}},)"
            R"("view":{"zoom":4.63121295979362912e-07}})"),
        "the view file that hung the application is rejected");
  CHECK(!studio::graph_view_is_sane(R"({"view":{"zoom":4.63e-07}})"),
        "a zoom that has collapsed to nothing is rejected");
  CHECK(!studio::graph_view_is_sane(
            R"({"nodes":{"n":{"location":{"x":-2147483648,"y":100}}}})"),
        "an INT_MIN node position is rejected");
  CHECK(!studio::graph_view_is_sane("{not json at all"),
        "an unparseable view file is rejected rather than handed over");

  // and the file is actually removed, since that is what unblocks the launch
  {
    std::string path = "test_graph_view_tmp.json";
    {
      std::ofstream f(path);
      f << R"({"view":{"zoom":4.63e-07}})";
    }
    studio::discard_insane_graph_view(path);
    CHECK(!std::filesystem::exists(path), "a bad view file is deleted");

    {
      std::ofstream f(path);
      f << R"({"view":{"zoom":1.0}})";
    }
    studio::discard_insane_graph_view(path);
    CHECK(std::filesystem::exists(path), "a good view file is left in place");
    std::error_code ec;
    std::filesystem::remove(path, ec);
  }
}

int main() {
  std::printf("Geekatplay TerraForge - undo/redo tests\n\n");
  test_graph_view_sanity();
  test_graph_undo();
  test_attributes_and_links();
  test_world_undo();
  test_scene_undo();
  test_mesh_vertices_survive();
  test_redo_branch_truncation();
  test_history_and_jump();
  test_stack_overflow_is_stable();
  test_undo_is_deterministic();
  test_planets_in_scene();
  test_planet_persistence();
  test_node_library_roundtrip();
  std::printf("\n%s (%d failures)\n", g_failures ? "FAILED" : "ALL PASSED",
              g_failures);
  return g_failures ? 1 : 0;
}

