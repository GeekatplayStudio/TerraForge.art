// Geekatplay TerraForge - a species as a thing a person owns: named from
// words, built from an archetype, saved to a file, dressed in pictures made
// from rules, and exported as a mesh (engine/gpx/plant.hpp).
//
// The promises, each asserted directly:
//   1. the words a person types name a real plant - "old oak" is an oak and
//      it is old - and words we do not know still grow something;
//   2. every archetype builds a graph whose root grows a plant;
//   3. a species saved and loaded back is the same species;
//   4. every leaf shape and bark kind makes a picture that is neither empty
//      nor solid, and a leaf's cut-out is an outline inside its picture;
//   5. a grown plant writes as OBJ and as glTF, and the glTF reads back.
#include "gpx/mesh_io.hpp"
#include "gpx/plant.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace gpx;

static int fails = 0;
static void check(bool ok, const std::string &what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what.c_str());
    ++fails;
  }
}

static void test_words() {
  std::printf("plants by name...\n");
  PlantDescription d;
  check(plant_describe_from_words("oak", d), "an oak is a plant we know");
  check(d.archetype == "broadleaf_tree", "and it is a broadleaf tree");
  check(d.height_m > 8.f, "of a real size: " + std::to_string(d.height_m));

  PlantDescription old_oak;
  plant_describe_from_words("old oak", old_oak);
  check(old_oak.age_years > d.age_years, "an old oak is older than an oak");

  PlantDescription palm;
  check(plant_describe_from_words("date palm", palm), "a date palm is known");
  check(palm.archetype == "palm", "and it is a palm");

  PlantDescription dead;
  plant_describe_from_words("dead pine in winter", dead);
  check(dead.archetype == "conifer" || dead.archetype == "dead_tree", "a dead pine is a conifer or a snag");

  PlantDescription nonsense;
  check(!plant_describe_from_words("zorblax", nonsense), "a word we do not know says so");
  check(!nonsense.archetype.empty(), "and still describes something");

  // the description survives its JSON
  const std::string text = plant_description_json(d);
  PlantDescription back;
  std::string err;
  check(plant_description_parse(text, back, err), "a description reads back from JSON: " + err);
  check(back.archetype == d.archetype && std::fabs(back.height_m - d.height_m) < 1e-3f, "unchanged");
  check(plant_description_schema().size() > 400, "the schema a model reads is written out");
}

static void test_archetypes() {
  std::printf("archetypes...\n");
  const std::vector<std::string> &kinds = plant_archetypes();
  check(kinds.size() >= 12, "there are archetypes for the main kinds of plant");
  for (const std::string &k : kinds) {
    Graph g;
    PlantDescription d;
    plant_describe_from_words(k, d);
    d.archetype = k;
    d.name = k;
    std::string err;
    const uint64_t root = plant_species_from_description(g, d, 0.f, 0.f, err);
    check(root != 0, k + " builds a species: " + err);
    if (!root) continue;
    const Node *r = g.find_node(root);
    check(r && r->type == "PlantSpecies", k + "'s root is a species root");
    if (!r) continue;
    check(plant_subtree(g, *r).size() >= 3, k + " has parts");
    PlantMesh m;
    PlantBuildOptions o;
    o.seed = 5;
    const bool ok = plant_build(g, *r, o, m, err);
    check(ok, k + " grows: " + err);
    check(m.vertex_count() > 30, k + " has geometry (" + std::to_string(m.vertex_count()) + " vertices)");
    check(m.height_m > 0.02f, k + " has a height");
  }
}

static void test_height() {
  std::printf("the height asked for...\n");
  // An archetype builds from botany and what its rules add up to is only
  // roughly the height in the description - a gnarled oak wanders as it
  // climbs and came out half the height of a straight one. The species
  // builder corrects for that, and a plant named without an age word is a
  // grown plant, not a tenth-of-its-lifespan sapling.
  struct Case {
    const char *words;
    float low, high; // metres the grown plant must stand between
  };
  static const Case CASES[] = {
      {"oak", 17.f, 27.f},      {"old gnarled oak in autumn", 18.f, 30.f},
      {"sapling oak", 0.2f, 1.f}, {"birch", 15.f, 25.f},
      {"pine", 19.f, 31.f},     {"lavender", 0.3f, 1.f},
      {"date palm", 15.f, 25.f}};
  for (const Case &c : CASES) {
    Graph g;
    PlantDescription d;
    plant_describe_from_words(c.words, d);
    std::string err;
    const uint64_t root = plant_species_from_description(g, d, 0.f, 0.f, err);
    if (!root) {
      check(false, std::string(c.words) + " grows a species: " + err);
      continue;
    }
    PlantMesh m;
    PlantBuildOptions o;
    o.seed = 1;
    if (!plant_build(g, *g.find_node(root), o, m, err)) {
      check(false, std::string(c.words) + " grows: " + err);
      continue;
    }
    check(m.height_m > c.low && m.height_m < c.high,
          std::string(c.words) + " stands " + std::to_string(m.height_m) + " m (wanted " +
              std::to_string(c.low) + ".." + std::to_string(c.high) + ")");
    // and it is made of wood and leaves, not a bare pole
    check(m.primitives >= 3, std::string(c.words) + " is more than a pole (" +
                                 std::to_string(m.primitives) + " parts grown)");
    // every piece of wood wears the species' bark, not the fallback brown:
    // one bark material, and it carries the picture made from its rules
    int barks = 0, pictured = 0;
    for (const PlantMaterial &mm : m.materials)
      if (mm.name == "Bark") {
        ++barks;
        if (!mm.rgba.empty() && mm.w > 0) ++pictured;
      }
    if (barks > 0) {
      check(barks == 1, std::string(c.words) + " wears one bark, not " + std::to_string(barks));
      check(pictured == barks, std::string(c.words) + "'s bark carries its picture");
    }
  }
  // a picture carries its own colour, and the material must not darken it
  // again - doing that squared a mid-brown bark into something near black
  Graph g;
  PlantDescription d;
  plant_describe_from_words("oak", d);
  std::string err;
  const uint64_t root = plant_species_from_description(g, d, 0.f, 0.f, err);
  PlantMesh m;
  PlantBuildOptions o;
  o.seed = 1;
  if (root && plant_build(g, *g.find_node(root), o, m, err))
    for (const PlantMaterial &mm : m.materials)
      if (!mm.rgba.empty())
        check(mm.color[0] > 0.99f && mm.color[1] > 0.99f && mm.color[2] > 0.99f,
              "'" + mm.name + "' leaves its colour to its picture");
}

// ------------------------------------------------- a tree looks like a tree
// Three ways a plant can be built correctly and still be wrong to look at,
// each found by putting one in front of a camera and each measured here.
static void test_reads_as_a_plant() {
  std::printf("a tree reads as a tree...\n");
  static const char *KINDS[] = {"scots pine", "oak", "birch"};
  for (const char *words : KINDS) {
    Graph g;
    PlantDescription d;
    plant_describe_from_words(words, d);
    std::string err;
    const uint64_t root = plant_species_from_description(g, d, 0.f, 0.f, err);
    if (!root) {
      check(false, std::string(words) + " grows: " + err);
      continue;
    }
    PlantMesh m;
    PlantBuildOptions o;
    o.seed = 1;
    if (!plant_build(g, *g.find_node(root), o, m, err)) {
      check(false, std::string(words) + " builds: " + err);
      continue;
    }

    // 1. A picture that is mostly holes is not foliage. A conifer's needle
    //    cards wore a single needle drawn across the whole picture - 6%
    //    opaque - so the card was 94% hole and a pine read as bare brown
    //    wood at every distance however many needles it had.
    for (const PlantMaterial &mat : m.materials) {
      if (mat.rgba.empty() || mat.w <= 0) continue;
      size_t opaque = 0;
      for (size_t i = 3; i < mat.rgba.size(); i += 4)
        if (mat.rgba[i] > 128) ++opaque;
      const double frac = (double)opaque / (double)(mat.rgba.size() / 4);
      if (!mat.alpha_cutout) {
        check(frac > 0.95, std::string(words) + ": '" + mat.name + "' is a solid picture");
        continue;
      }
      check(frac > 0.15, std::string(words) + ": '" + mat.name + "' covers enough of its card to read as "
                             "foliage (" + std::to_string(frac * 100.0) + "%)");
    }

    // 2. What the plant is mostly made of should be what you mostly see. A
    //    pine's cones came to 467,000 triangles - nearly half the tree - and
    //    being the one part with no picture on them they painted the whole
    //    crown a flat brown.
    size_t total = 0;
    std::map<std::string, size_t> by_material;
    for (const PlantPart &p : m.parts) {
      total += p.index_count / 3;
      const PlantMaterial *mat = p.material >= 0 && p.material < (int)m.materials.size()
                                     ? &m.materials[(size_t)p.material] : nullptr;
      by_material[mat ? mat->name : std::string("?")] += p.index_count / 3;
    }
    check(total > 1000, std::string(words) + " has a mesh");
    if (!total) continue;
    for (const auto &kv : by_material) {
      if (kv.first != "Fruit") continue;
      const double share = (double)kv.second / (double)total;
      check(share < 0.15, std::string(words) + ": its fruit is a garnish, not the tree (" +
                              std::to_string(share * 100.0) + "% of its triangles)");
    }
    // and the foliage outweighs the wood, as a living tree's does
    const size_t leaf = by_material.count("Leaf") ? by_material["Leaf"] : 0;
    const size_t bark = by_material.count("Bark") ? by_material["Bark"] : 0;
    if (d.leaf_density > 0.1f && !d.archetype.empty() && d.archetype != "dead_tree")
      check(leaf > bark / 2, std::string(words) + ": there is foliage on it (" + std::to_string(leaf) +
                                 " against " + std::to_string(bark) + " of wood)");
  }
}

static void test_species_file() {
  std::printf("species files...\n");
  Graph g;
  PlantDescription d;
  plant_describe_from_words("birch", d);
  std::string err;
  const uint64_t root = plant_species_from_description(g, d, 0.f, 0.f, err);
  if (!root) {
    check(false, "a birch to save: " + err);
    return;
  }
  const Node *r = g.find_node(root);
  const std::vector<const Node *> before = plant_subtree(g, *r);
  PlantSpeciesInfo info;
  info.id = "birch";
  info.name = "Birch";
  info.group = "trees";
  info.height_m = 14.f;
  const fs::path dir = fs::temp_directory_path() / "tf_species_test";
  std::error_code ec;
  fs::remove_all(dir, ec);
  const std::string path = plant_species_save(g, *r, info, dir.string(), err);
  check(!path.empty(), "it saves: " + err);
  if (path.empty()) return;
  check(fs::exists(path), "the file is there");

  PlantSpeciesInfo read;
  check(plant_species_read_info(path, read, err), "its record reads: " + err);
  check(read.name == "Birch" && read.group == "trees", "with the name and the shelf");

  Graph g2;
  PlantSpeciesInfo loaded;
  const uint64_t root2 = plant_species_load(g2, path, 100.f, 50.f, loaded, err);
  check(root2 != 0, "it loads into another graph: " + err);
  if (!root2) return;
  const Node *r2 = g2.find_node(root2);
  check(r2 && r2->type == "PlantSpecies", "as a species root");
  if (!r2) return;
  const std::vector<const Node *> after = plant_subtree(g2, *r2);
  check(after.size() == before.size(), "with every part it had");
  PlantMesh m1, m2;
  PlantBuildOptions o;
  o.seed = 9;
  plant_build(g, *r, o, m1, err);
  plant_build(g2, *r2, o, m2, err);
  check(m1.vertex_count() == m2.vertex_count() && m1.triangle_count() == m2.triangle_count(),
        "and grows the same plant");
  fs::remove_all(dir, ec);
}

static void test_textures() {
  std::printf("pictures made from rules...\n");
  const char *shapes[] = {"ovate", "lanceolate", "lobed",  "palmate", "needle", "scale", "pinnate",
                          "heart", "linear",     "round",  "elliptic", "frond",  "blade"};
  for (const char *shape : shapes) {
    PlantLeafTexture t;
    t.shape = shape;
    t.seed = 3;
    std::vector<uint8_t> rgba;
    std::vector<float> cut;
    check(plant_texture_leaf(t, 96, 96, rgba, cut), std::string("a ") + shape + " leaf is drawn");
    check(rgba.size() == 96u * 96u * 4u, std::string(shape) + ": the picture is the size asked for");
    if (rgba.size() != 96u * 96u * 4u) continue;
    size_t solid = 0;
    for (size_t i = 3; i < rgba.size(); i += 4)
      if (rgba[i] > 128) ++solid;
    const float coverage = (float)solid / (96.f * 96.f);
    check(coverage > 0.05f && coverage < 0.95f,
          std::string(shape) + ": the leaf covers part of its picture (" + std::to_string(coverage) + ")");
    check(cut.size() >= 16, std::string(shape) + ": its cut-out is an outline");
    bool inside = true;
    for (float v : cut) inside = inside && v >= -0.01f && v <= 1.01f;
    check(inside, std::string(shape) + ": the outline is inside the picture");
  }
  // A leaf is not a printed card: it has a surface. The midrib stands proud
  // with the blade dished either side, the veins ridge across it, and the
  // cuticle is waxy on the blade and dull along the veins - and none of that
  // is in the colour. Without the two maps a leaf is lit as one flat facet
  // however good its picture, which is most of what made our foliage read as
  // paper next to a photograph.
  {
    PlantLeafTexture t;
    t.shape = "ovate";
    t.seed = 7;
    std::vector<uint8_t> rgba, nrm, rough;
    std::vector<float> cut;
    check(plant_texture_leaf(t, 128, 128, rgba, cut, &nrm, &rough), "a leaf hands back its surface");
    check(nrm.size() == rgba.size(), "a normal per texel");
    check(rough.size() == rgba.size(), "and a roughness");
    // the normal map must actually turn: a flat one is no better than none
    int turned = 0;
    float zmin = 2.f;
    for (size_t i = 0; i + 3 < nrm.size(); i += 4) {
      const float x = nrm[i] / 255.f * 2.f - 1.f, y = nrm[i + 1] / 255.f * 2.f - 1.f;
      const float z = nrm[i + 2] / 255.f * 2.f - 1.f;
      if (std::fabs(x) > 0.06f || std::fabs(y) > 0.06f) ++turned;
      zmin = std::min(zmin, z);
    }
    check(turned > (int)(nrm.size() / 4) / 50, "the surface turns where the veins are (" +
                                                   std::to_string(turned) + " texels)");
    check(zmin > 0.f, "and never turns away from the leaf's own face");
    // the cuticle varies: a leaf with one roughness everywhere has no glints
    int lo = 255, hi = 0;
    for (size_t i = 0; i < rough.size(); i += 4) {
      lo = std::min(lo, (int)rough[i]);
      hi = std::max(hi, (int)rough[i]);
    }
    check(hi - lo > 20, "the blade is glossier than its veins (" + std::to_string(hi - lo) + ")");
    // and a dying leaf loses its shine
    PlantLeafTexture dry = t;
    dry.wilt = 1.f;
    std::vector<uint8_t> drgba, dnrm, drough;
    std::vector<float> dcut;
    plant_texture_leaf(dry, 64, 64, drgba, dcut, &dnrm, &drough);
    double wet = 0, dryness = 0;
    for (size_t i = 0; i < rough.size(); i += 4) wet += rough[i];
    for (size_t i = 0; i < drough.size(); i += 4) dryness += drough[i];
    check(dryness / std::max<size_t>(drough.size() / 4, 1) >
              wet / std::max<size_t>(rough.size() / 4, 1),
          "a wilting leaf is duller than a fresh one");
  }

  const char *barks[] = {"fissured", "plated", "smooth", "birch", "ringed", "peeling", "scaly", "fibrous"};
  for (const char *kind : barks) {
    PlantBarkTexture b;
    b.kind = kind;
    b.seed = 5;
    std::vector<uint8_t> rgba, nrm;
    check(plant_texture_bark(b, 64, 64, rgba, nrm), std::string("a ") + kind + " bark is drawn");
    if (rgba.size() != 64u * 64u * 4u) {
      check(false, std::string(kind) + ": the picture is the size asked for");
      continue;
    }
    check(nrm.size() == rgba.size(), std::string(kind) + ": it has a normal map");
    // it must vary, and it must meet itself at the seam
    int lo = 255, hi = 0;
    long seam = 0;
    for (int y = 0; y < 64; ++y) {
      for (int x = 0; x < 64; ++x) {
        const int v = rgba[((size_t)y * 64 + (size_t)x) * 4];
        lo = std::min(lo, v);
        hi = std::max(hi, v);
      }
      seam += std::abs((int)rgba[((size_t)y * 64) * 4] - (int)rgba[((size_t)y * 64 + 63) * 4]);
    }
    check(hi - lo > 12, std::string(kind) + ": the bark has grain");
    check(seam / 64 < 60, std::string(kind) + ": its edges meet (" + std::to_string(seam / 64) + ")");
  }
}

static void test_export() {
  std::printf("export...\n");
  Graph g;
  PlantDescription d;
  plant_describe_from_words("shrub", d);
  std::string err;
  const uint64_t root = plant_species_from_description(g, d, 0.f, 0.f, err);
  if (!root) {
    check(false, "a shrub to export: " + err);
    return;
  }
  PlantMesh m;
  PlantBuildOptions o;
  o.seed = 2;
  if (!plant_build(g, *g.find_node(root), o, m, err)) {
    check(false, "it grows: " + err);
    return;
  }
  const fs::path dir = fs::temp_directory_path() / "tf_plant_export";
  std::error_code ec;
  fs::create_directories(dir, ec);
  const std::string obj = (dir / "plant.obj").string(), glb = (dir / "plant.glb").string();
  check(plant_mesh_write(m, obj, err), "it writes OBJ: " + err);
  check(fs::exists(obj) && fs::file_size(obj, ec) > 1000, "the OBJ has content");
  check(plant_mesh_write(m, glb, err), "it writes glTF: " + err);
  check(fs::exists(glb) && fs::file_size(glb, ec) > 1000, "the glTF has content");
  TriMesh back;
  if (fs::exists(glb)) {
    const bool read = mesh_load_glb(glb, back, err);
    check(read, "and reads back into our own loader: " + err);
    if (read)
      check(back.vert_count() >= m.vertex_count() / 2,
            "with the vertices it was given (" + std::to_string(back.vert_count()) + " of " +
                std::to_string(m.vertex_count()) + ")");
  }
  fs::remove_all(dir, ec);
}

int test_plant_species_all() {
  fails = 0;
  test_words();
  test_archetypes();
  test_height();
  test_reads_as_a_plant();
  test_species_file();
  test_textures();
  test_export();
  return fails;
}
