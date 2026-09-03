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
#include "autosave.hpp"
#include "undo.hpp"
#include "gpx/metanode.hpp"
#include "gpx/node_graph.hpp"
#include "gpx/serialization.hpp"
#include <json.hpp>
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

// render_settings() now lives in studio/render_settings.cpp - GL-free on
// purpose, so this suite links the real one instead of a stub.

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

static void test_lights_and_primitives_undo() {
  std::printf("undo: lights, primitives and scatter bindings...\n");
  App a;
  reset_all(a);
  int before = (int)scene().objects.size();

  undo_push(a, "Add light");
  int li = scene_add_light("Lamp");
  scene().objects[li].light_type = 1;
  scene().objects[li].light_cone = 55.f;
  CHECK((int)scene().objects.size() == before + 1, "light added");

  undo_push(a, "Add primitive");
  int pi = scene_add_primitive("sphere", "Ball");
  gpx::Node *sp = a.graph.add_node("ScatterPoints", 0, 0);
  scene().objects[pi].scatter_node = sp ? sp->id : 0;
  scene().objects[pi].scatter_sway = 0.1f;
  CHECK((int)scene().objects.size() == before + 2, "primitive added");

  undo_perform(a);
  CHECK((int)scene().objects.size() == before + 1,
        "undo removed the primitive (and its scatter binding with it)");
  undo_perform(a);
  CHECK((int)scene().objects.size() == before, "undo removed the light");

  redo_perform(a);
  redo_perform(a);
  CHECK((int)scene().objects.size() == before + 2, "redo restored both");
  bool light_ok = false, prim_ok = false;
  for (const SceneObject &o : scene().objects) {
    if (o.name == "Lamp")
      light_ok = o.type == SceneObject::Light && o.light_type == 1 &&
                 o.light_cone == 55.f;
    if (o.name == "Ball")
      prim_ok = o.type == SceneObject::Mesh && o.vert_count > 100 &&
                o.scatter_node != 0 && o.scatter_sway == 0.1f;
  }
  CHECK(light_ok, "the spot light came back with cone and type intact");
  CHECK(prim_ok, "the primitive came back with geometry and scatter binding");
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

// An OBJ is an untrusted file, and its face indices were used directly as
// &pos[i*3]. "f 99999" in a three-vertex file read whatever was past the end
// of the heap, and OBJ's negative (relative) indices parsed through
// sscanf("%u") as 4294967295. Neither was checked.
//
// These write real files rather than calling an internal helper, because the
// parser is the thing being tested and the file is where it meets the parser.
static void test_obj_import_rejects_bad_indices() {
  std::printf("[obj import bounds]\n");
  namespace fs = std::filesystem;
  fs::path dir = fs::temp_directory_path() / "gpx_obj_test";
  fs::create_directories(dir);

  auto write_obj = [&](const char *name, const char *body) {
    fs::path f = dir / name;
    std::ofstream o(f);
    o << body;
    o.close();
    return f.string();
  };

  struct Case { const char *name; const char *body; bool should_load; };
  const Case cases[] = {
      {"ok.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", true},
      {"past_end.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 4\n", false},        // one past the end
      {"way_past.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 99999\n", false},         // read arbitrary heap before
      {"relative.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf -3 -2 -1\n", true},          // -1 is the last vertex so far
      {"relative_bad.obj", "v 0 0 0\nv 1 0 0\nf -5 -1 -2\n", false},  // reaches before the first
      {"zero.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 0 1 2\n", false},            // OBJ indices are 1-based
      {"garbage.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf a b c\n", false},      // no digits at all
      {"slashes.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1/1/1 2/2/2 3/3/3\n", true},         // a/t/n must still work
  };

  for (const Case &c : cases) {
    std::string path = write_obj(c.name, c.body);
    std::string err;
    int idx = studio::scene_import_obj(path, err);
    if (c.should_load)
      CHECK(idx >= 0, std::string(c.name) + " should import but failed: " + err);
    else
      CHECK(idx < 0, std::string(c.name) +
                         " imported a face index that does not exist");
  }
  std::error_code ec;
  fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------- transforms
// scene_object_matrix is the one definition of where an object is: the
// renderer draws with it, picking and the selection outline measure with it,
// and the Properties editor types into the numbers it reads. A silent change
// to the rotation order or the scale convention would move every imported
// object in every scene, so the convention is pinned here.
static void test_object_matrix() {
  std::printf("scene_object_matrix\n");
  using studio::SceneObject;
  auto xform = [](const float *m, float x, float y, float z, float *out) {
    for (int r = 0; r < 3; ++r)
      out[r] = m[0 * 4 + r] * x + m[1 * 4 + r] * y + m[2 * 4 + r] * z +
               m[3 * 4 + r];
  };
  float m[16], n[9], p[3];

  // identity: no rotation, unit size, and the altitude scaled by height_scale
  {
    SceneObject o;
    o.pos[0] = 0.25f; o.pos[1] = 0.4f; o.pos[2] = 0.75f;
    o.scale = 1.f;
    studio::scene_object_matrix(o, 0.5f, m, n);
    xform(m, 0.f, 0.f, 0.f, p);
    CHECK(std::fabs(p[0] - 0.25f) < 1e-6f, "origin lands at pos.x");
    CHECK(std::fabs(p[1] - 0.20f) < 1e-6f, "altitude is scaled by height_scale");
    CHECK(std::fabs(p[2] - 0.75f) < 1e-6f, "origin lands at pos.z");
    xform(m, 1.f, 0.f, 0.f, p);
    CHECK(std::fabs(p[0] - 1.25f) < 1e-6f, "unrotated +X stays +X");
  }

  // heading 90 degrees turns local +X into world -Z (right-handed Ry)
  {
    SceneObject o;
    o.pos[0] = o.pos[1] = o.pos[2] = 0.f;
    o.scale = 1.f;
    o.yaw = 90.f;
    studio::scene_object_matrix(o, 1.f, m, n);
    xform(m, 1.f, 0.f, 0.f, p);
    CHECK(std::fabs(p[0]) < 1e-5f && std::fabs(p[1]) < 1e-5f &&
              std::fabs(p[2] + 1.f) < 1e-5f,
          "heading 90 maps +X to -Z");
  }

  // pitch tips the nose, bank rolls it, and HPB order is Ry*Rx*Rz
  {
    SceneObject o;
    o.pos[0] = o.pos[1] = o.pos[2] = 0.f;
    o.scale = 1.f;
    o.pitch = 90.f;
    studio::scene_object_matrix(o, 1.f, m, n);
    xform(m, 0.f, 1.f, 0.f, p);
    CHECK(std::fabs(p[1]) < 1e-5f && std::fabs(p[2] - 1.f) < 1e-5f,
          "pitch 90 maps +Y to +Z");
    SceneObject b;
    b.pos[0] = b.pos[1] = b.pos[2] = 0.f;
    b.scale = 1.f;
    b.roll = 90.f;
    studio::scene_object_matrix(b, 1.f, m, n);
    xform(m, 1.f, 0.f, 0.f, p);
    CHECK(std::fabs(p[0]) < 1e-5f && std::fabs(p[1] - 1.f) < 1e-5f,
          "bank 90 maps +X to +Y");
  }

  // non-uniform scale stretches the geometry and shrinks the normal by the
  // reciprocal - that is what keeps a squashed object shaded correctly
  {
    SceneObject o;
    o.pos[0] = o.pos[1] = o.pos[2] = 0.f;
    o.scale = 2.f;
    o.scl[0] = 2.f; o.scl[1] = 0.5f; o.scl[2] = 1.f;
    studio::scene_object_matrix(o, 1.f, m, n);
    xform(m, 1.f, 1.f, 1.f, p);
    CHECK(std::fabs(p[0] - 4.f) < 1e-5f, "x scaled by scale*scl.x");
    CHECK(std::fabs(p[1] - 1.f) < 1e-5f, "y scaled by scale*scl.y");
    CHECK(std::fabs(p[2] - 2.f) < 1e-5f, "z scaled by scale*scl.z");
    CHECK(std::fabs(n[0] - 0.25f) < 1e-5f, "normal x uses 1/(scale*scl.x)");
    CHECK(std::fabs(n[4] - 1.f) < 1e-5f, "normal y uses 1/(scale*scl.y)");
    CHECK(std::fabs(studio::scene_object_radius(o) - 4.f) < 1e-5f,
          "radius is scale * largest axis");
  }

  // whatever the angles, the rotation stays orthonormal: no shear, no drift
  {
    SceneObject o;
    o.pos[0] = o.pos[1] = o.pos[2] = 0.f;
    o.scale = 1.f;
    o.yaw = 37.f; o.pitch = -64.f; o.roll = 121.f;
    studio::scene_object_matrix(o, 1.f, m, n);
    for (int c = 0; c < 3; ++c) {
      float len = 0;
      for (int r = 0; r < 3; ++r) len += m[c * 4 + r] * m[c * 4 + r];
      CHECK(std::fabs(std::sqrt(len) - 1.f) < 1e-5f, "column is unit length");
    }
    for (int c = 0; c < 3; ++c) {
      int d = (c + 1) % 3;
      float dot = 0;
      for (int r = 0; r < 3; ++r) dot += m[c * 4 + r] * m[d * 4 + r];
      CHECK(std::fabs(dot) < 1e-5f, "columns are orthogonal");
    }
  }
}

// ------------------------------------------------------- scene persistence
// The defect this exists to hold shut: a saved project used to carry the
// graph plus planets, and nothing else. Imported meshes vanished on load,
// with their transforms and material bindings; every camera beyond the
// default was gone with its lens; the whole environment reset. Set up a
// sunset over a lake with a hero camera, save, reload: flat noon over
// defaults. Each of those is asserted here individually, so whichever one
// regresses is the one named.
static void test_scene_persistence() {
  std::printf("scene persistence: meshes, cameras, environment...\n");
  namespace fs = std::filesystem;
  fs::path dir = fs::temp_directory_path() / "gpx_persist_test";
  std::error_code ec;
  fs::create_directories(dir, ec);
  const std::string obj_path = (dir / "rock.obj").string();
  const std::string proj_path = (dir / "scene.gpxt").string();

  {
    std::ofstream f(obj_path);
    f << "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\n"
         "f 1 2 3\nf 1 3 4\nf 1 4 2\nf 2 4 3\n";
  }

  App a;
  project_new(a);
  scene() = SceneState{};
  scene_init_builtins();
  SceneState &sc = scene();

  // a mesh with every transform the gizmo and Properties can write
  std::string err;
  int mi = scene_import_obj(obj_path, err);
  CHECK(mi >= 0, "test OBJ imports");
  if (mi >= 0) {
    SceneObject &m = sc.objects[mi];
    m.name = "Hero rock";
    m.pos[0] = 0.31f; m.pos[1] = 0.12f; m.pos[2] = 0.77f;
    m.scale = 0.042f;
    m.scl[0] = 2.f; m.scl[1] = 0.5f; m.scl[2] = 1.25f;
    m.yaw = 33.f; m.pitch = -12.f; m.roll = 4.5f;
    m.color[0] = 0.9f; m.color[1] = 0.2f; m.color[2] = 0.1f;
    gpx::Node *spn = a.graph.add_node("ScatterPoints", 0, 0);
    m.scatter_node = spn ? spn->id : 0;
    m.scatter_scale = 0.7f;
    m.scatter_jitter = 0.55f;
    m.scatter_seed = 9u;
  }

  // a built-in primitive: no file on disk, must regenerate on load
  {
    int pi = scene_add_primitive("sphere", "Orb");
    CHECK(pi >= 0 && sc.objects[pi].vert_count > 100,
          "the primitive generates geometry");
  }

  // a second camera with a changed lens and render assignment
  int ci = scene_add_camera("Hero cam");
  CHECK(ci >= 0, "camera added");
  if (ci >= 0) {
    CameraData &c = sc.objects[ci].cam;
    c.focal_mm = 85.f;
    c.aperture = 1.4f;
    c.iso = 800.f;
    c.render.width = 3840;
    c.render.height = 2160;
    c.render.samples = 42;
    c.render.output = "hero.png";
    scene_active_camera() = ci;
  }

  // a custom layer, and the mesh on it
  sc.layers.push_back({"Props", false});
  if (mi >= 0) sc.objects[mi].layer = (int)sc.layers.size() - 1;

  // a planet, so the old format's subject matter is in the new one too
  int pi = scene_add_planet("Redworld");
  if (pi >= 0) {
    sc.objects[pi].planet.radius = 7.5f;
    sc.objects[pi].planet.seed = 4242;
  }
  sc.selected = mi;

  // the environment: the sunset-over-a-lake half of the defect
  RenderSettings &rs = render_settings();
  rs = RenderSettings{};
  rs.sun_azimuth = 261.f;
  rs.sun_altitude = 7.f;
  rs.fog_type = 2;
  rs.fog_density = 3.3f;
  rs.water_level = 0.41f;
  rs.terrain_size_m = 12000.f;
  rs.height_scale = 0.31f;
  rs.cloud_coverage = 0.83f;
  rs.units = 1;

  const size_t n_objects = sc.objects.size();
  CHECK(project_save(a, proj_path), "project saves");

  // scorch the earth, then load
  scene() = SceneState{};
  scene_init_builtins();
  rs = RenderSettings{};
  CHECK(project_load(a, proj_path), "project loads");

  SceneState &sl = scene();
  CHECK(sl.objects.size() == n_objects, "every object came back");

  int lm = -1, lc = -1, lp = -1;
  for (int i = 0; i < (int)sl.objects.size(); ++i) {
    if (sl.objects[i].name == "Hero rock") lm = i;
    if (sl.objects[i].name == "Hero cam") lc = i;
    if (sl.objects[i].name == "Redworld") lp = i;
  }
  int lo_ = -1;
  for (int i = 0; i < (int)sl.objects.size(); ++i)
    if (sl.objects[i].name == "Orb") lo_ = i;
  CHECK(lo_ >= 0 && sl.objects[lo_].vert_count > 100,
        "the primitive regenerated from its kind on load");
  CHECK(lm >= 0, "the imported mesh survived the round trip");
  if (lm >= 0) {
    const SceneObject &m = sl.objects[lm];
    CHECK(m.vert_count == 12, "its geometry was re-imported from its path");
    CHECK(std::fabs(m.pos[0] - 0.31f) < 1e-6f && m.scale == 0.042f,
          "position and size survive");
    CHECK(m.scl[0] == 2.f && m.scl[1] == 0.5f && m.scl[2] == 1.25f,
          "the per-axis squeeze survives");
    CHECK(m.yaw == 33.f && m.pitch == -12.f && m.roll == 4.5f,
          "heading, pitch and bank survive");
    CHECK(m.layer == (int)sl.layers.size() - 1, "its layer assignment survives");
    CHECK(m.scatter_node != 0 && m.scatter_scale == 0.7f &&
              m.scatter_jitter == 0.55f && m.scatter_seed == 9u,
          "the scatter binding survives, node id remapped");
  }
  CHECK(sl.layers.size() == 2 && sl.layers[1].name == "Props" &&
            !sl.layers[1].visible,
        "custom layers survive, visibility included");

  CHECK(lc >= 0, "the second camera survived");
  if (lc >= 0) {
    const CameraData &c = sl.objects[lc].cam;
    CHECK(c.focal_mm == 85.f && c.aperture == 1.4f && c.iso == 800.f,
          "its lens survives");
    CHECK(c.render.width == 3840 && c.render.samples == 42 &&
              c.render.output == "hero.png",
          "its render assignment survives");
    CHECK(scene_active_camera() == lc, "and it is still the active camera");
  }

  CHECK(lp >= 0 && sl.objects[lp].planet.radius == 7.5f &&
            sl.objects[lp].planet.seed == 4242u,
        "the planet still round-trips in the new format");

  CHECK(rs.sun_azimuth == 261.f && rs.sun_altitude == 7.f,
        "the sun survives");
  CHECK(rs.fog_type == 2 && rs.fog_density == 3.3f, "the fog survives");
  CHECK(rs.water_level == 0.41f, "the water level survives");
  CHECK(rs.terrain_size_m == 12000.f && rs.units == 1,
        "world scale and units survive");
  CHECK(rs.height_scale == 0.31f && rs.cloud_coverage == 0.83f,
        "height scale and clouds survive");

  // a mesh whose file has gone missing keeps its place instead of vanishing
  {
    fs::remove(obj_path, ec);
    CHECK(project_load(a, proj_path), "project loads with the OBJ gone");
    int gm = -1;
    for (int i = 0; i < (int)scene().objects.size(); ++i)
      if (scene().objects[i].name == "Hero rock") gm = i;
    CHECK(gm >= 0, "the mesh object is kept, empty, rather than dropped");
    if (gm >= 0) {
      CHECK(scene().objects[gm].vert_count == 0, "with no geometry");
      CHECK(scene().objects[gm].scale == 0.042f,
            "but its transform still there for when the file returns");
    }
  }

  // an old-format file (graph + scene_bodies only) still loads its planets
  {
    std::ifstream f(proj_path);
    std::string text((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    f.close();
    // strip the new sections, leaving what an old build would have written
    auto cut = [&](const char *key) {
      size_t k = text.find(std::string("\"") + key + "\":");
      if (k == std::string::npos) return;
      // crude but sufficient: reserialize through json to drop the key
    };
    (void)cut;
    nlohmann::json j = nlohmann::json::parse(text);
    j.erase("scene");
    j.erase("environment");
    const std::string old_path = (dir / "old_format.gpxt").string();
    std::ofstream o(old_path);
    o << j.dump(1);
    o.close();
    CHECK(project_load(a, old_path), "an old-format project still loads");
    bool planet_back = false;
    for (const SceneObject &ob : scene().objects)
      if (ob.type == SceneObject::Planet && ob.name == "Redworld")
        planet_back = true;
    CHECK(planet_back, "and its planets still appear");
  }

  fs::remove_all(dir, ec);
}

// ------------------------------------------------------------------ autosave
// The rules that make autosave trustworthy, asserted one by one: it saves
// only when the history has moved, it rotates three slots instead of
// overwriting one, it never touches the user's own status line or project
// path, and a lock file left behind is what makes the next start offer the
// newest slot back.
static void test_autosave() {
  std::printf("autosave and crash recovery...\n");
  namespace fs = std::filesystem;
  fs::path dir = fs::temp_directory_path() / "gpx_autosave_test";
  std::error_code ec;
  fs::remove_all(dir, ec);
  autosave_set_dir(dir.string());

  App a;
  reset_all(a);
  {
    std::lock_guard<std::mutex> lk(a.graph_mtx);
    a.graph.add_node("Noise", 0, 0);
  }
  a.status = "user status";
  a.project_path = "user_project.gpxt";

  // nothing in the history yet moved since the baseline: first tick saves
  // (history state is unknown), second tick with no edits does not
  undo_push(a, "edit 1");
  autosave_tick(a, 1000.0, 120.0);
  CHECK(fs::exists(dir / "autosave_1.gpxt", ec), "first autosave written");
  CHECK(a.status == "user status", "the user's status line is untouched");
  CHECK(a.project_path == "user_project.gpxt",
        "and so is the project path - Save still goes to the user's file");

  autosave_tick(a, 2000.0, 120.0);
  CHECK(!fs::exists(dir / "autosave_2.gpxt", ec),
        "no edit since the last autosave means no new file");

  // an edit moves the history; the next interval writes the NEXT slot
  undo_push(a, "edit 2");
  autosave_tick(a, 3000.0, 120.0);
  CHECK(fs::exists(dir / "autosave_2.gpxt", ec), "rotates to the second slot");

  // inside the interval nothing happens even with edits pending
  undo_push(a, "edit 3");
  autosave_tick(a, 3001.0, 120.0);
  CHECK(!fs::exists(dir / "autosave_3.gpxt", ec),
        "the interval is respected");

  // slot arithmetic wraps
  CHECK(autosave_next_slot(0) == 1 && autosave_next_slot(2) == 0,
        "three slots, in rotation");

  // crash detection: begin twice without an end = the first never exited
  autosave_session_begin();
  CHECK(!fs::exists(dir / "nothing", ec), "sanity");
  {
    std::string path;
    CHECK(!autosave_crash_recovery_available(path),
          "a clean history offers nothing");
  }
  autosave_session_begin(); // the lock from the line above is still there
  {
    std::string path;
    CHECK(autosave_crash_recovery_available(path),
          "a leftover lock plus an autosave offers recovery");
    CHECK(path.find("autosave_2") != std::string::npos,
          "and offers the newest slot");
    // the restored file really is a loadable project
    App b;
    reset_all(b);
    CHECK(project_load(b, path), "the offered autosave loads");
    bool has_noise = false;
    {
      std::lock_guard<std::mutex> lk(b.graph_mtx);
      for (auto &n : b.graph.nodes)
        if (n->type == "Noise") has_noise = true;
    }
    CHECK(has_noise, "and contains the session's work");
  }
  // THE data-loss case: while recovery is unanswered, a tick must write
  // nothing - the first tick of a fresh session fires immediately and would
  // otherwise overwrite the very file being offered back
  {
    undo_push(a, "edit while dialog is up");
    autosave_tick(a, 9000.0, 120.0);
    std::string path;
    CHECK(autosave_crash_recovery_available(path),
          "recovery still on offer after a tick");
    CHECK(path.find("autosave_2") != std::string::npos,
          "and still offering the crashed session, not a fresh overwrite");
  }
  autosave_mark_recovery_answered();
  {
    std::string path;
    CHECK(!autosave_crash_recovery_available(path),
          "answered once means not asked again");
  }
  autosave_session_end();
  CHECK(!fs::exists(dir / "session.lock", ec), "an orderly end removes the lock");

  fs::remove_all(dir, ec);
}

int test_undo_render_run(); // test_undo_render.cpp

int main() {
  std::printf("Geekatplay TerraForge - undo/redo tests\n\n");
  g_failures += test_undo_render_run();
  test_graph_view_sanity();
  test_graph_undo();
  test_attributes_and_links();
  test_world_undo();
  test_scene_undo();
  test_lights_and_primitives_undo();
  test_mesh_vertices_survive();
  test_redo_branch_truncation();
  test_history_and_jump();
  test_stack_overflow_is_stable();
  test_undo_is_deterministic();
  test_planets_in_scene();
  test_planet_persistence();
  test_node_library_roundtrip();
  test_obj_import_rejects_bad_indices();
  test_object_matrix();
  test_scene_persistence();
  test_autosave();
  std::printf("\n%s (%d failures)\n", g_failures ? "FAILED" : "ALL PASSED",
              g_failures);
  return g_failures ? 1 : 0;
}

