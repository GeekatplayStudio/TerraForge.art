// Geekatplay TerraForge - the studio half of MaterialSource: turning the
// name of a scene object into the id of the material it is wearing.
//
// The engine suite proves the node reads whatever id it is given, in the
// right order. This proves it is given the right id - which is the half the
// user actually operates, because what they touch is a dropdown of object
// names. Linked into undo_tests.
#include "material_source.hpp"
#include "scene.hpp"
#include "scene_io.hpp"
#include <cstdio>
#include <string>

using namespace studio;

static int g_fail = 0;
#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("  [FAIL] %s (line %d)\n", std::string(msg).c_str(),         \
                  __LINE__);                                                   \
      g_fail++;                                                                \
    }                                                                          \
  } while (0)

namespace {

SceneObject named(const char *name, SceneObject::Type t) {
  SceneObject o;
  o.name = name;
  o.type = t;
  return o;
}

// A graph with one importer in it, and the importer.
gpx::Node *importer(gpx::Graph &g, const char *object) {
  gpx::Node *n = g.add_node("MaterialSource");
  if (gpx::Attribute *a = n->attrs.find("object")) a->s = object;
  return n;
}

std::string source_of(gpx::Node *n) {
  const gpx::Attribute *a = n->attrs.find("source_node");
  return a ? a->s : std::string("<missing>");
}

void test_the_named_objects_material_is_the_one_found() {
  std::printf("material source (scene): the named object's material...\n");
  SceneState sc;
  sc.objects.push_back(named("Terrain", SceneObject::Terrain));
  sc.objects.back().material_node = 11;
  sc.objects.push_back(named("Boulder", SceneObject::Mesh));
  sc.objects.back().material_node = 22;

  CHECK(material_of_object(sc, "Terrain", 0) == 11, "the terrain's material");
  CHECK(material_of_object(sc, "Boulder", 0) == 22, "the boulder's material");
  CHECK(material_of_object(sc, "Nothing", 0) == 0, "a name nobody has");
  CHECK(material_of_object(sc, "", 0) == 0, "no name at all");
}

// A project made before objects carried a material recorded the terrain's in
// the render settings. Naming the terrain has to find it there, or every such
// project imports an empty material and the feature looks broken on exactly
// the files people already have.
void test_an_older_project_still_finds_its_terrain_material() {
  std::printf("material source (scene): older projects...\n");
  SceneState sc;
  sc.objects.push_back(named("Terrain", SceneObject::Terrain));
  sc.objects.back().material_node = 0; // never assigned one of its own
  CHECK(material_of_object(sc, "Terrain", 77) == 77,
        "falls back to the render settings' terrain material");

  // But only for the terrain, and only when the object has none: an object's
  // own material always wins, or assigning one would appear to do nothing.
  sc.objects.back().material_node = 5;
  CHECK(material_of_object(sc, "Terrain", 77) == 5, "its own material wins");
  sc.objects.push_back(named("Boulder", SceneObject::Mesh));
  CHECK(material_of_object(sc, "Boulder", 77) == 0,
        "a mesh with no material does not borrow the terrain's");
}

void test_resolving_writes_the_id_and_dirties_the_node() {
  std::printf("material source (scene): resolving...\n");
  SceneState sc;
  sc.objects.push_back(named("Terrain", SceneObject::Terrain));
  gpx::Graph g;
  g.resolution = 16;
  gpx::Node *mat = g.add_node("MaterialOutput");
  sc.objects.back().material_node = mat->id;
  gpx::Node *imp = importer(g, "Terrain");

  CHECK(material_sources_resolve(g, sc, 0) == 1, "one node resolved");
  CHECK(source_of(imp) == std::to_string(mat->id),
        "the material's id was written: " + source_of(imp));
  CHECK(imp->dirty, "and the node was marked for recompute");

  // Idempotent: a service that runs every frame must not report a change,
  // and must not dirty the graph, when nothing has moved. It would re-evaluate
  // the whole project sixty times a second.
  imp->dirty = false;
  CHECK(material_sources_resolve(g, sc, 0) == 0, "a second pass changes nothing");
  CHECK(!imp->dirty, "and leaves the node clean");
}

// Reassigning the object's material has to reach the importer.
void test_a_new_material_on_the_object_is_followed() {
  std::printf("material source (scene): following a reassignment...\n");
  SceneState sc;
  sc.objects.push_back(named("Terrain", SceneObject::Terrain));
  gpx::Graph g;
  g.resolution = 16;
  gpx::Node *a = g.add_node("MaterialOutput");
  gpx::Node *b = g.add_node("MaterialOutput");
  sc.objects.back().material_node = a->id;
  gpx::Node *imp = importer(g, "Terrain");
  material_sources_resolve(g, sc, 0);

  sc.objects.back().material_node = b->id; // the user assigns another
  CHECK(material_sources_resolve(g, sc, 0) == 1, "the change was noticed");
  CHECK(source_of(imp) == std::to_string(b->id),
        "the importer follows the object, not the node it first found");
}

// The load-order trap. A project is read before its scene is populated; a
// service that clears every unresolved name would empty the attribute in that
// window and the material would be gone by the time the objects arrived.
void test_an_empty_scene_changes_nothing() {
  std::printf("material source (scene): an empty scene...\n");
  SceneState sc; // nothing loaded yet
  gpx::Graph g;
  g.resolution = 16;
  gpx::Node *imp = importer(g, "Terrain");
  if (gpx::Attribute *a = imp->attrs.find("source_node")) a->s = "42";
  CHECK(material_sources_resolve(g, sc, 0) == 0, "nothing to resolve against");
  CHECK(source_of(imp) == "42", "the saved id survives until the scene loads");
}

// But once there is a scene, a name that is not in it is genuinely wrong and
// has to be cleared - otherwise deleting the object leaves the importer
// quietly serving the material of a thing that no longer exists.
void test_a_deleted_object_clears_the_import() {
  std::printf("material source (scene): a deleted object...\n");
  SceneState sc;
  sc.objects.push_back(named("Terrain", SceneObject::Terrain));
  gpx::Graph g;
  g.resolution = 16;
  gpx::Node *mat = g.add_node("MaterialOutput");
  sc.objects.back().material_node = mat->id;
  gpx::Node *imp = importer(g, "Boulder"); // gone from the scene
  if (gpx::Attribute *a = imp->attrs.find("source_node"))
    a->s = std::to_string(mat->id); // ...but still pointing at a material
  CHECK(material_sources_resolve(g, sc, 0) == 1, "the stale id is noticed");
  CHECK(source_of(imp).empty(),
        "and cleared, rather than left serving the wrong material");
}

// The assignment itself has to survive being saved.
//
// It did not: the material binding was written inside the branch for Mesh
// objects, so assigning a material to the terrain - which is what the
// Materials panel's Assign button does unless another object is named -
// worked all session and was gone the next time the project was opened.
// Nothing reported it, because an object with no material is a normal object.
void test_a_material_assignment_survives_a_save() {
  std::printf("material source (scene): assignment round trip...\n");
  scene() = SceneState{};
  SceneState &sc = scene();
  sc.objects.push_back(named("Terrain", SceneObject::Terrain));
  sc.objects.back().material_node = 7;
  sc.objects.push_back(named("Boulder", SceneObject::Mesh));
  sc.objects.back().material_node = 9;
  sc.objects.push_back(named("Key light", SceneObject::Light));

  nlohmann::json j = scene_to_json();
  scene() = SceneState{};
  std::string warn;
  // An identity map: the ids in the file are the ids that come back, which is
  // what a save/load inside one session looks like.
  GraphIdMap idmap{{7, 7}, {9, 9}};
  scene_from_json(j, idmap, warn);
  SceneState &back = scene();
  // a scene with a terrain in it is given its home planet on load
  // (scene_ensure_home_planet), appended after the objects that were saved
  CHECK(back.objects.size() == 4 && back.objects[3].type == SceneObject::Planet &&
            back.objects[3].planet.home,
        "every object came back, and the world under them");
  if (back.objects.size() < 3) return;
  CHECK(back.objects[0].material_node == 7,
        "the terrain kept its material: " +
            std::to_string(back.objects[0].material_node));
  CHECK(back.objects[1].material_node == 9, "and so did the mesh");
  CHECK(back.objects[2].material_node == 0, "an object with none still has none");
  scene() = SceneState{};
}

} // namespace

int test_material_source_scene_run() {
  test_the_named_objects_material_is_the_one_found();
  test_an_older_project_still_finds_its_terrain_material();
  test_resolving_writes_the_id_and_dirties_the_node();
  test_a_new_material_on_the_object_is_followed();
  test_an_empty_scene_changes_nothing();
  test_a_deleted_object_clears_the_import();
  test_a_material_assignment_survives_a_save();
  return g_fail;
}
