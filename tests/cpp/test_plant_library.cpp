// Geekatplay TerraForge - the plant library (studio/plant_library.cpp).
//
// The promises, each asserted directly:
//   1. a manifest reads whole - names, licence, sizes, its variants in name
//      order with their files resolved against the plant's folder;
//   2. what is not a manifest is refused, with a reason;
//   3. the folder scan lists the plants that can be placed and nothing else:
//      a broken manifest, or a download whose model never arrived, is left
//      out, and a variant whose file is missing is dropped from its set;
//   4. search needs every word, in any of name, group, tags or categories;
//   5. the built-ins are the kinds scene_plants.cpp builds, at their sizes;
//   6. recording a model writes a manifest that scans back, with its units
//      read from the file or from the height asked for.
#include "plant_library.hpp"
#include "scene.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace studio;
namespace fs = std::filesystem;

static int failures = 0;
static void check(bool ok, const char *what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

static void write(const fs::path &p, const std::string &text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}

static const char *FERN = R"({
 "id": "fern_02", "name": "Fern 02", "source": "polyhaven",
 "url": "https://polyhaven.com/a/fern_02", "license": "CC0 1.0",
 "authors": ["Someone"], "categories": ["plants", "nature"], "group": "ground cover",
 "tags": ["leafy", "forest"], "polycount": 6232, "height_m": 0.397, "unit_m": 1.0,
 "model": "fern_02_1k.gltf", "thumbnail": "thumb.png",
 "variants": [
  {"id": "c", "name": "c", "model": "fern_02_1k_c.gltf", "height_m": 0.32, "polycount": 2000},
  {"id": "a", "name": "a", "model": "fern_02_1k_a.gltf", "height_m": 0.26, "polycount": 400},
  {"id": "b", "name": "b", "model": "fern_02_1k_b.gltf", "height_m": 0.40, "polycount": 2100}
 ]
})";

static void test_manifest() {
  std::printf("manifest...\n");
  const std::string folder = (fs::temp_directory_path() / "tf_plants_manifest" / "fern_02").string();
  PlantEntry e;
  std::string err;
  check(plant_manifest_parse(FERN, folder, e, err), "a full manifest reads");
  check(e.id == "polyhaven/fern_02", "the id carries its source");
  check(e.name == "Fern 02" && e.group == "ground cover" && e.license == "CC0 1.0", "name, group, licence");
  check(std::fabs(e.height_m - 0.397f) < 1e-4f && e.polycount == 6232, "height and polycount");
  check(e.model == (fs::path(folder) / "fern_02_1k.gltf").string(), "the model resolves against the folder");
  check(e.thumb.empty(), "a picture that is not there is not named");
  check(e.variants.size() == 3, "three variants");
  if (e.variants.size() == 3) {
    check(e.variants[0].id == "a" && e.variants[1].id == "b" && e.variants[2].id == "c", "variants in name order");
    check(e.variants[1].model == (fs::path(folder) / "fern_02_1k_b.gltf").string(), "a variant's file resolves");
    check(plant_variant(e, "B") == &e.variants[1], "a variant by id, any case");
    check(plant_variant(e, "") == &e.variants[0], "no request is the first");
    check(plant_variant(e, "zzz") == &e.variants[0], "an unknown request is the first");
  }
  check(plant_matches(e, "fern forest"), "every word found");
  check(plant_matches(e, "GROUND"), "the group is searched, any case");
  check(!plant_matches(e, "fern palm"), "a missing word fails the search");
  check(plant_matches(e, ""), "an empty search matches");

  PlantEntry u;
  check(plant_manifest_parse(R"({"id":"oak","model":"C:/models/oak.fbx","height_m":14})", folder, u, err),
        "a minimal manifest reads");
  check(u.id == "user/oak" && u.license == "your own", "a manifest without a source is the user's own");
  check(u.group == "trees", "no group: a 14 m plant is on the trees shelf");
  check(u.unit_m == 1.f, "units default to metres");

  PlantEntry bad;
  err.clear();
  check(!plant_manifest_parse("[1, 2]", folder, bad, err) && !err.empty(), "an array is refused with a reason");
  err.clear();
  check(!plant_manifest_parse(R"({"id":"x"})", folder, bad, err) && !err.empty(), "no model is refused");
  check(!plant_manifest_parse("{not json", folder, bad, err), "broken JSON is refused");
}

static void test_scan() {
  std::printf("scan...\n");
  const fs::path root = fs::temp_directory_path() / "tf_plants_scan";
  std::error_code ec;
  fs::remove_all(root, ec);
  // a complete plant with one of its variants missing
  write(root / "polyhaven" / "fern_02" / "plant.json", FERN);
  write(root / "polyhaven" / "fern_02" / "fern_02_1k.gltf", "{}");
  write(root / "polyhaven" / "fern_02" / "fern_02_1k_a.gltf", "{}");
  write(root / "polyhaven" / "fern_02" / "fern_02_1k_b.gltf", "{}");
  write(root / "polyhaven" / "fern_02" / "thumb.png", "png");
  // a download whose model never arrived
  write(root / "polyhaven" / "half" / "plant.json", R"({"id":"half","name":"Half","model":"half.gltf"})");
  // a manifest that does not read
  write(root / "polyhaven" / "broken" / "plant.json", "{oops");
  // a folder with no manifest at all
  write(root / "polyhaven" / "stray" / "readme.txt", "hello");
  // a second source, sorting before the fern by name
  write(root / "user" / "aspen" / "plant.json", R"({"id":"aspen","name":"Aspen","model":"aspen.obj"})");
  write(root / "user" / "aspen" / "aspen.obj", "v 0 0 0");

  const std::vector<PlantEntry> found = plant_scan(root.string());
  check(found.size() == 2, "two plants can be placed");
  if (found.size() == 2) {
    check(found[0].name == "Aspen" && found[1].name == "Fern 02", "listed by name");
    check(found[1].variants.size() == 2, "the variant without a file is dropped");
    check(!found[1].thumb.empty(), "the picture that is there is named");
  }
  check(plant_scan((root / "nowhere").string()).empty(), "a missing library is empty, not an error");
}

static void test_builtins() {
  std::printf("built-ins...\n");
  const std::vector<PlantEntry> b = plant_builtins();
  check(b.size() == 7, "seven built-in plants");
  for (const PlantEntry &p : b) {
    check(p.id == "builtin/" + p.kind, "a built-in's id is its kind");
    check(scene_is_plant_kind(p.kind), "every built-in is a kind scene_plants.cpp builds");
    check(p.height_m == scene_plant_size_m(p.kind), "its height is the size it is built at");
    check(!p.group.empty() && p.model.empty(), "a shelf, and no file");
  }
  const std::vector<std::string> &g = plant_groups();
  check(!g.empty() && g.front() == "all", "the shelves start with all");
  for (const PlantEntry &p : b)
    check(std::find(g.begin(), g.end(), p.group) != g.end(), "a built-in's shelf is a shelf the panel shows");
}

static void test_record() {
  std::printf("record a model...\n");
  const fs::path root = fs::temp_directory_path() / "tf_plants_record";
  std::error_code ec;
  fs::remove_all(root, ec);
  // a tall thin triangle, 180 units up: a model in centimetres
  const fs::path model = root / "models" / "My Oak.obj";
  write(model, "v 0 0 0\nv 1 0 0\nv 0 180 0\nf 1 2 3\n");
  std::string err;
  const std::string id = plant_record_model((root / "lib").string(), model.string(), "", 0.f, err);
  check(id == "user/my_oak", ("the id comes from the file's name (" + id + " " + err + ")").c_str());
  const std::string id2 = plant_record_model((root / "lib").string(), model.string(), "My Oak", 12.f, err);
  check(id2 == "user/my_oak_2", "a second record of the same name gets its own id");
  const std::vector<PlantEntry> found = plant_scan((root / "lib").string());
  check(found.size() == 2, "both records scan back");
  for (const PlantEntry &p : found) {
    check(p.source == "user" && p.license == "your own", "a record is the user's own");
    check(fs::path(p.model) == fs::absolute(model), "the model stays where it is");
    if (p.id == "user/my_oak") {
      check(std::fabs(p.unit_m - 0.01f) < 1e-6f, "180 units tall reads as centimetres");
      check(std::fabs(p.height_m - 1.8f) < 1e-3f && p.group == "shrubs", "1.8 m: a shrub");
    } else {
      check(std::fabs(p.unit_m - 12.f / 180.f) < 1e-6f, "a height asked for sets the units");
      check(p.group == "trees", "12 m: a tree");
    }
  }
  err.clear();
  check(plant_record_model((root / "lib").string(), (root / "none.obj").string(), "", 0.f, err).empty() &&
            !err.empty(),
        "a file that is not there is refused");
}

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  test_manifest();
  test_scan();
  test_builtins();
  test_record();
  if (failures) {
    std::printf("%d plant library check(s) failed\n", failures);
    return 1;
  }
  std::printf("plant library tests passed\n");
  return 0;
}
