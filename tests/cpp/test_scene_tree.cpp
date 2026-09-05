// Geekatplay TerraForge - Object Manager model tests: the three-state
// visibility dots, scene_move_object's index fix-up, layer colours and
// old-file compatibility. Linked into undo_tests.
#include "scene.hpp"
#include "scene_io.hpp"
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

static int add(SceneState &sc, const char *name, int parent,
               SceneObject::Type t = SceneObject::Mesh) {
  SceneObject o;
  o.type = t;
  o.name = name;
  o.parent = parent;
  sc.objects.push_back(o);
  return (int)sc.objects.size() - 1;
}

static void test_visibility_states() {
  std::printf("object manager: three-state visibility...\n");
  SceneState sc;
  int root = add(sc, "root", -1, SceneObject::Group);
  int mid = add(sc, "mid", root, SceneObject::Group);
  int leaf = add(sc, "leaf", mid);
  CHECK(sc.object_visible(sc.objects[leaf]), "all grey = visible");
  CHECK(sc.object_render_visible(sc.objects[leaf]), "all grey = rendered");

  sc.objects[root].vis_viewport = 2; // red parent
  CHECK(!sc.object_visible(sc.objects[leaf]), "grey child of a red root is hidden");
  CHECK(!sc.object_visible(sc.objects[mid]), "grey mid under a red root is hidden");
  CHECK(sc.object_render_visible(sc.objects[leaf]),
        "the render dot is independent of the viewport dot");

  sc.objects[mid].vis_viewport = 1; // green beats the red parent
  CHECK(sc.object_visible(sc.objects[mid]), "green child under a red parent is visible");
  CHECK(sc.object_visible(sc.objects[leaf]), "grey grandchild inherits the green");

  sc.objects[leaf].vis_viewport = 2;
  CHECK(!sc.object_visible(sc.objects[leaf]), "red child under a green parent is hidden");
  sc.objects[leaf].vis_viewport = 0;

  sc.objects[root].vis_render = 2;
  CHECK(!sc.object_render_visible(sc.objects[leaf]), "render: red root hides grey leaf");
  sc.objects[leaf].vis_render = 1;
  CHECK(sc.object_render_visible(sc.objects[leaf]), "render: green leaf beats red root");

  sc.objects[leaf].enabled = false;
  CHECK(!sc.object_visible(sc.objects[leaf]), "enabled=false hides in the viewport");
  CHECK(!sc.object_render_visible(sc.objects[leaf]), "enabled=false hides in the render");
  sc.objects[leaf].enabled = true;

  sc.layers[0].visible = false;
  CHECK(!sc.object_visible(sc.objects[leaf]), "a hidden layer still hides");
  sc.layers[0].visible = true;
  sc.objects[leaf].visible = false;
  CHECK(!sc.object_visible(sc.objects[leaf]), "the old visible flag still hides");
}

// Every index that points into the array must still name the same object
// after a move to each possible position.
static void test_move_object() {
  std::printf("object manager: scene_move_object index fix-up...\n");
  for (int to = 0; to < 6; ++to) {
    scene() = SceneState{};
    SceneState &sc = scene();
    int g = add(sc, "G", -1, SceneObject::Group);       // 0
    int a = add(sc, "A", g);                             // 1
    int cams = add(sc, "Cams", -1, SceneObject::Group);  // 2
    int c1 = add(sc, "C1", cams, SceneObject::Camera);   // 3
    int c2 = add(sc, "C2", cams, SceneObject::Camera);   // 4
    int b = add(sc, "B", g);                             // 5
    (void)a;
    sc.selected = c2;
    sc.selection = {c2, b};
    scene_active_camera() = c1;
    scene_last_used_camera() = c2;

    int moved = scene_move_object(b, to, cams); // B becomes a child of Cams
    CHECK(moved == to, "move lands at the asked position");
    CHECK(sc.objects.size() == 6, "nothing lost");
    auto find = [&](const char *n) {
      for (int i = 0; i < (int)sc.objects.size(); ++i)
        if (sc.objects[i].name == n) return i;
      return -1;
    };
    CHECK(sc.objects[find("B")].parent == find("Cams"), "B is under Cams");
    CHECK(sc.objects[find("A")].parent == find("G"), "A still under G");
    CHECK(sc.objects[find("C1")].parent == find("Cams"), "C1 still under Cams");
    CHECK(sc.objects[find("C2")].parent == find("Cams"), "C2 still under Cams");
    CHECK(sc.objects[find("G")].parent == -1, "G still at the root");
    CHECK(sc.objects[sc.selected].name == "C2", "selected follows C2");
    CHECK(sc.selection.size() == 2 && sc.objects[sc.selection[0]].name == "C2" &&
              sc.objects[sc.selection[1]].name == "B",
          "the multi-selection follows its objects");
    CHECK(sc.objects[scene_active_camera()].name == "C1", "active camera follows C1");
    CHECK(sc.objects[scene_last_used_camera()].name == "C2", "last camera follows C2");
  }
  {
    scene() = SceneState{};
    SceneState &sc = scene();
    int g = add(sc, "G", -1, SceneObject::Group);
    int k = add(sc, "K", g, SceneObject::Group);
    int kk = add(sc, "KK", k);
    CHECK(scene_move_object(g, 0, kk) == -1, "a move under a descendant is refused");
    CHECK(scene_move_object(g, 0, g) == -1, "a move under itself is refused");
    CHECK(scene_move_object(kk, 0, -1) == 0, "moving to the root works");
    CHECK(sc.objects[0].name == "KK" && sc.objects[0].parent == -1, "KK is first, at the root");
  }
  scene() = SceneState{};
  scene_active_camera() = -1;
  scene_last_used_camera() = -1;
}

static void test_layer_colour_and_old_files() {
  std::printf("object manager: layer colour round trip, old files...\n");
  scene() = SceneState{};
  SceneState &sc = scene();
  add(sc, "thing", -1);
  SceneLayer l{"Rocks", true};
  l.color[0] = 0.1f; l.color[1] = 0.6f; l.color[2] = 0.9f;
  sc.layers.push_back(l);
  sc.objects[0].layer = 1;
  sc.objects[0].vis_viewport = 1;
  sc.objects[0].vis_render = 2;
  sc.objects[0].enabled = false;
  nlohmann::json j = scene_to_json();
  scene() = SceneState{};
  std::string warn;
  scene_from_json(j, GraphIdMap{}, warn);
  SceneState &sc2 = scene();
  CHECK(sc2.layers.size() == 2, "both layers came back");
  CHECK(sc2.layers.size() == 2 && sc2.layers[1].color[0] == 0.1f &&
            sc2.layers[1].color[1] == 0.6f && sc2.layers[1].color[2] == 0.9f,
        "the layer colour round-trips");
  CHECK(!sc2.objects.empty() && sc2.objects[0].vis_viewport == 1 &&
            sc2.objects[0].vis_render == 2 && !sc2.objects[0].enabled,
        "the three states round-trip");

  // a file from before the dots: "visible": false becomes a red viewport dot
  nlohmann::json old = {
      {"layers", {{{"name", "Default"}, {"visible", true}}}},
      {"objects", {{{"kind", "mesh"}, {"name", "old"}, {"visible", false}}}}};
  scene() = SceneState{};
  scene_from_json(old, GraphIdMap{}, warn);
  SceneState &sc3 = scene();
  CHECK(!sc3.objects.empty() && sc3.objects[0].vis_viewport == 2,
        "old visible=false loads as vis_viewport 2");
  CHECK(!sc3.objects.empty() && !sc3.object_visible(sc3.objects[0]),
        "and the object is hidden");
  CHECK(!sc3.layers.empty() && sc3.layers[0].color[0] > 0.f,
        "a layer without a colour gets one from the palette");
  scene() = SceneState{};
}

int test_scene_tree_run() {
  test_visibility_states();
  test_move_object();
  test_layer_colour_and_old_files();
  return g_fail;
}
