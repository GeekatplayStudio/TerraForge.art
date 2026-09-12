// Geekatplay TerraForge - what a plant part wears
// (plant_internal.hpp: BuildCtx::material_for, the defaults).
//
// A part names its material by slot: 0..3 are its "material" .. "material 4"
// inputs, 4 its cap material, 5 and 6 its blade materials. A slot with a
// PlantMaterial node on it becomes one of the mesh's materials; a slot with
// nothing becomes a default chosen by what the part is - wood for a segment,
// leaf for a leaf, petal for a flower, a berry red for a ball - so a species
// half built still looks like a plant rather than grey soup.
//
// Two things here are worth knowing. First, seasons: a material with
// seasonal looks is not one material that changes, it is one mesh material
// per look, and the look the season selects is the one a part gets. That
// keeps the mesh honest - every triangle names the pictures it is actually
// drawn with - and it is what lets one species export an autumn plant and a
// summer plant from the same graph.
//
// Second, pictures made from rules: a material whose Source is Leaf, Bark or
// Petal has no file at all, it has a generator (plant_textures.cpp). Those
// are memoised by a hash of every parameter that goes into them, because a
// plant with two thousand leaves asks for the same leaf picture two thousand
// times and the generator costs milliseconds. The memo is keyed by value,
// not by node, so two species that ask for the same oak leaf share it.
#include "plant/plant_internal.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>

namespace gpx {
namespace plant {

namespace {

// The ports a slot means, in slot order (plant_internal.hpp's mapping).
const char *const SLOT_PORT[7] = {"material", "material 2", "material 3", "material 4",
                                  "cap material", "blade material", "blade material 2"};

const char *const LEAF_SHAPES[13] = {"ovate", "lanceolate", "lobed",  "palmate", "needle",
                                     "scale", "pinnate",    "heart",  "linear",  "round",
                                     "elliptic", "frond",   "blade"};
const char *const BARK_KINDS[8] = {"fissured", "plated", "smooth",  "birch",
                                   "ringed",   "peeling", "scaly",  "fibrous"};

int registered(BuildCtx &ctx, PlantMaterial m) {
  if (!ctx.mesh) return 0;
  for (size_t i = 0; i < ctx.mesh->materials.size(); ++i)
    if (ctx.mesh->materials[i].node == m.node && ctx.mesh->materials[i].name == m.name)
      return (int)i;
  ctx.mesh->materials.push_back(std::move(m));
  return (int)ctx.mesh->materials.size() - 1;
}

PlantMaterial wood_material() {
  PlantMaterial m;
  m.name = "Bark";
  m.color[0] = 0.35f; m.color[1] = 0.28f; m.color[2] = 0.2f; m.color[3] = 1.f;
  m.roughness = 0.9f;
  return m;
}
PlantMaterial leaf_material() {
  PlantMaterial m;
  m.name = "Leaf";
  m.color[0] = 0.25f; m.color[1] = 0.45f; m.color[2] = 0.15f; m.color[3] = 1.f;
  m.roughness = 0.6f;
  m.translucency = 0.45f;
  m.backlight = 0.35f;
  m.two_sided = true;
  return m;
}
PlantMaterial petal_material() {
  PlantMaterial m;
  m.name = "Petal";
  m.color[0] = 0.95f; m.color[1] = 0.85f; m.color[2] = 0.3f; m.color[3] = 1.f;
  m.roughness = 0.45f;
  m.translucency = 0.6f;
  m.two_sided = true;
  return m;
}
PlantMaterial fruit_material() {
  PlantMaterial m;
  m.name = "Fruit";
  m.color[0] = 0.72f; m.color[1] = 0.14f; m.color[2] = 0.11f; m.color[3] = 1.f;
  m.roughness = 0.35f;
  return m;
}

// ---------------------------------------------------------------- pictures
struct Picture {
  std::vector<uint8_t> rgba, normal, rough;
  std::vector<float> cutout;
  int w = 0, h = 0;
};

std::mutex &memo_mutex() {
  static std::mutex m;
  return m;
}
std::map<uint64_t, Picture> &memo() {
  static std::map<uint64_t, Picture> m;
  return m;
}

uint64_t mix(uint64_t h, float v) {
  uint32_t bits;
  std::memcpy(&bits, &v, sizeof bits);
  return (h ^ bits) * 1099511628211ull;
}
uint64_t mix(uint64_t h, int v) { return (h ^ (uint64_t)(uint32_t)v) * 1099511628211ull; }

int texture_size(int choice) {
  switch (choice) {
    case 0: return 256;
    case 2: return 1024;
    case 3: return 2048;
    default: return 512;
  }
}

// The picture a "made from rules" material asks for, made once per set of
// values however many parts wear it.
const Picture &picture_for(const Node &n, int source, float wilt, int size) {
  const AttrSet &a = n.attrs;
  uint64_t key = 1469598103934665603ull;
  key = mix(key, source);
  key = mix(key, size);
  key = mix(key, wilt);
  key = mix(key, (int)a.get_seed("tex_seed"));
  for (const char *k : {"leaf_shape", "leaf_lobe_count", "bark_kind"}) key = mix(key, a.get_i(k, 0));
  for (const char *k : {"leaf_serration", "leaf_lobes", "leaf_aspect", "leaf_vein", "leaf_mottle", "bark_scale",
                        "roughness"})
    key = mix(key, a.get_f(k, 0.f));
  for (const char *k : {"color", "vein_color", "crack_color"})
    if (const Attribute *at = a.find(k))
      for (int c = 0; c < 3; ++c) key = mix(key, at->col[c]);
  {
    std::lock_guard<std::mutex> lock(memo_mutex());
    auto it = memo().find(key);
    if (it != memo().end()) return it->second;
  }
  Picture p;
  p.w = p.h = size;
  float color[4] = {0.5f, 0.5f, 0.5f, 1.f};
  if (const Attribute *at = a.find("color"))
    for (int c = 0; c < 4; ++c) color[c] = at->col[c];
  if (source == 2) { // bark
    PlantBarkTexture t;
    t.kind = BARK_KINDS[std::clamp(a.get_choice("bark_kind"), 0, 7)];
    for (int c = 0; c < 3; ++c) t.color[c] = color[c];
    if (const Attribute *at = a.find("crack_color"))
      for (int c = 0; c < 3; ++c) t.crack_color[c] = at->col[c];
    t.scale = a.get_f("bark_scale", 1.f);
    t.roughness = a.get_f("roughness", 0.6f);
    t.seed = a.get_seed("tex_seed");
    plant_texture_bark(t, size, size, p.rgba, p.normal);
  } else if (source == 3) { // petal
    float base[3] = {std::min(color[0] + 0.25f, 1.f), std::min(color[1] + 0.3f, 1.f),
                     std::min(color[2] + 0.2f, 1.f)};
    plant_texture_petal(color, base, a.get_seed("tex_seed"), size, size, p.rgba, p.cutout);
  } else { // leaf
    PlantLeafTexture t;
    t.shape = LEAF_SHAPES[std::clamp(a.get_choice("leaf_shape"), 0, 12)];
    for (int c = 0; c < 3; ++c) t.color[c] = color[c];
    if (const Attribute *at = a.find("vein_color"))
      for (int c = 0; c < 3; ++c) t.vein_color[c] = at->col[c];
    t.serration = a.get_f("leaf_serration", 0.f);
    t.lobes = a.get_f("leaf_lobes", 0.5f);
    t.lobe_count = a.get_i("leaf_lobe_count", 5);
    t.aspect = a.get_f("leaf_aspect", 0.55f);
    t.vein_strength = a.get_f("leaf_vein", 0.5f);
    t.mottle = a.get_f("leaf_mottle", 0.3f);
    t.wilt = wilt;
    t.seed = a.get_seed("tex_seed");
    // with its surface: the veins and the dished blade as a normal map, and
    // the waxy cuticle as roughness. Without those a leaf is one flat facet.
    plant_texture_leaf(t, size, size, p.rgba, p.cutout, &p.normal, &p.rough);
  }
  std::lock_guard<std::mutex> lock(memo_mutex());
  return memo()[key] = std::move(p);
}

// "u,v;u,v;..." as the cut-out outline a material carries by hand.
std::vector<float> parse_cutout(const std::string &s) {
  std::vector<float> out;
  size_t i = 0;
  while (i < s.size()) {
    const size_t semi = std::min(s.find(';', i), s.size());
    const std::string part = s.substr(i, semi - i);
    const size_t comma = part.find(',');
    if (comma != std::string::npos) {
      out.push_back((float)std::atof(part.substr(0, comma).c_str()));
      out.push_back((float)std::atof(part.substr(comma + 1).c_str()));
    }
    i = semi + 1;
  }
  if (out.size() < 6) out.clear(); // fewer than three points is not an outline
  return out;
}

// One seasonal look of a material node, as the manual's Seasonal Material
// Editor describes it: its own pictures and a hue, luminosity and
// saturation shift on the base colour.
PlantSeasonLook look_of(const Node &n, int index) {
  const AttrSet &a = n.attrs;
  const std::string p = "s" + std::to_string(index + 1) + "_";
  PlantSeasonLook L;
  L.name = a.get_s(p + "name");
  L.color_map = a.get_s(p + "color_map");
  L.alpha_map = a.get_s(p + "alpha_map");
  L.normal_map = a.get_s(p + "normal_map");
  L.shift[0] = a.get_f(p + "hue", 0.f);
  L.shift[1] = a.get_f(p + "lum", 0.f);
  L.shift[2] = a.get_f(p + "sat", 0.f);
  L.presence = a.get_f(p + "presence", 1.f);
  return L;
}

} // namespace

int default_wood_material(BuildCtx &ctx) { return registered(ctx, wood_material()); }
int default_leaf_material(BuildCtx &ctx) { return registered(ctx, leaf_material()); }
int default_petal_material(BuildCtx &ctx) { return registered(ctx, petal_material()); }

int BuildCtx::material_for(const Node &part, int slot) {
  const std::vector<const Node *> mats = materials_of(part);
  const int s = std::clamp(slot, 0, 6);
  const Node *m = s < (int)mats.size() ? mats[(size_t)s] : nullptr;
  if (!m) {
    // nothing connected: what this kind of part wears by default
    const Kind k = kind_of(part);
    if (s >= 5) return default_leaf_material(*this); // blades are foliage
    switch (k) {
      case Kind::Leaf: case Kind::CutoutLeaf: case Kind::Warpboard: return default_leaf_material(*this);
      case Kind::Flower: return default_petal_material(*this);
      case Kind::Ball: return registered(*this, fruit_material());
      case Kind::Urchin: return default_leaf_material(*this);
      default: return default_wood_material(*this);
    }
  }
  // which seasonal look this season selects
  const int count = std::clamp(m->attrs.get_i("season_count", 0), 0, 4);
  int look = -1;
  if (count > 0) {
    const Attribute *curve = m->attrs.find("season_curve");
    float pick = 0.f;
    if (curve && !curve->curves.empty()) pick = curve->curves.primary().eval(clampf(season, 0.f, 1.f));
    look = std::clamp((int)std::lround(pick), 0, count - 1);
  }
  const uint64_t key = (m->id << 3) | (uint64_t)(look + 1);
  auto it = material_index.find(key);
  if (it != material_index.end()) return it->second;

  PlantMaterial out;
  out.node = m->id;
  out.name = m->attrs.get_s("name");
  if (out.name.empty()) out.name = "Material " + std::to_string(m->id);
  const Attribute *col = m->attrs.find("color");
  for (int c = 0; c < 4; ++c) out.color[c] = col ? col->col[c] : 0.5f;
  out.roughness = m->attrs.get_f("roughness", 0.7f);
  out.metallic = m->attrs.get_f("metallic", 0.f);
  out.translucency = m->attrs.get_f("translucency", 0.f);
  out.backlight = m->attrs.get_f("backlight", 0.f);
  out.two_sided = m->attrs.get_b("two_sided", false);
  out.alpha_cutout = m->attrs.get_b("alpha_cutout", true);
  out.color_map = m->attrs.get_s("color_map");
  out.alpha_map = m->attrs.get_s("alpha_map");
  out.normal_map = m->attrs.get_s("normal_map");
  out.roughness_map = m->attrs.get_s("roughness_map");
  out.uv_tile[0] = m->attrs.get_f("u_tile", 1.f);
  out.uv_tile[1] = m->attrs.get_f("v_tile", 1.f);
  out.cutout = parse_cutout(m->attrs.get_s("cutout"));
  if (const Attribute *sc = m->attrs.find("season_curve"))
    if (!sc->curves.empty()) out.season_curve = sc->curves.primary();
  for (int i = 0; i < count; ++i) out.seasons.push_back(look_of(*m, i));

  // the look's own pictures and colour shift
  if (look >= 0 && look < (int)out.seasons.size()) {
    const PlantSeasonLook &L = out.seasons[(size_t)look];
    out.name += L.name.empty() ? "" : " - " + L.name;
    if (!L.color_map.empty()) out.color_map = L.color_map;
    if (!L.alpha_map.empty()) out.alpha_map = L.alpha_map;
    if (!L.normal_map.empty()) out.normal_map = L.normal_map;
    hls_shift(out.color, L.shift[0], L.shift[1], L.shift[2]);
  }

  // a picture made from rules: no file, the pixels ride with the material
  const int source = m->attrs.get_choice("source");
  if (source > 0) {
    const float wilt = std::round(clampf(1.f - health, 0.f, 1.f) * 10.f) / 10.f;
    const int size = texture_size(m->attrs.get_choice("tex_size"));
    const Picture &pic = picture_for(*m, source, source == 2 ? 0.f : wilt, size);
    if (!pic.rgba.empty()) {
      out.rgba = pic.rgba;
      out.normal_rgba = pic.normal;
      out.rough_rgba = pic.rough;
      out.w = pic.w;
      out.h = pic.h;
      out.color_map.clear();
      out.alpha_map.clear();
      out.alpha_cutout = source != 2; // bark is opaque; a leaf or a petal is cut out
      if (out.cutout.empty() && m->attrs.get_b("auto_cutout", true)) out.cutout = pic.cutout;
      // The picture carries the colour - a bark is drawn with its own colour
      // between its cracks, a leaf with its own green - so the material's
      // colour must not be multiplied in on top of it. Doing that squared a
      // mid-brown bark into something near black.
      for (int c = 0; c < 3; ++c) out.color[c] = 1.f;
    }
  }
  const int index = registered(*this, std::move(out));
  material_index[key] = index;
  return index;
}

} // namespace plant
} // namespace gpx
