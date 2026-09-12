// Geekatplay TerraForge - plants a person can simply name.
//
// "oak". "date palm". "old weeping willow in autumn". Typing a plant's
// name should grow that plant, with no model to call and nothing to
// configure, because the botany of the hundred and forty plants people
// actually ask for is small enough to write down: how tall an oak grows,
// that a birch's bark is white and its leaves turn yellow, that a saguaro
// puts out arms at seventy years and flowers for a fortnight in May.
//
// The table (plant_knowledge_table.cpp, plant_knowledge_table2.cpp) holds
// only what makes each plant itself; everything else comes from its
// archetype's defaults, which are written here as table rows of the same
// shape. The longest name wins, so "japanese maple" beats "maple" and
// "weeping willow" beats "willow" - a shorter name must never quietly
// answer for a longer one the user actually typed.
//
// The words around the name are modifiers: an age, a health, a season, a
// size, a habit. Age is a field of the description and is set outright.
// Health and season are not - the description says what a species IS, not
// what one individual looks like this morning - so they are approximated
// in the fields that do exist (a dying tree becomes the dead-tree
// archetype, an autumn crown wears its autumn colour) and the request is
// written into the note, where the studio and the assistant can see it.
//
// Every plant here is written from botany. Nothing is transcribed from
// another tool's catalogue.
#include "plant/plant_knowledge.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace gpx {
namespace plant {
namespace know {

namespace {

void hex_rgb(uint32_t v, float out[3]) {
  out[0] = (float)((v >> 16) & 0xff) / 255.f;
  out[1] = (float)((v >> 8) & 0xff) / 255.f;
  out[2] = (float)(v & 0xff) / 255.f;
}

// Letters and digits, everything else a space, one space between words,
// with a space at each end so " oak " can only match a whole word.
std::string clean(const std::string &s) {
  std::string out = " ";
  for (char c : s) {
    const unsigned char u = (unsigned char)c;
    if (std::isalnum(u)) out += (char)std::tolower(u);
    else if (out.back() != ' ') out += ' ';
  }
  if (out.back() != ' ') out += ' ';
  return out;
}

bool has_word(const std::string &words, const std::string &w) {
  if (w.empty()) return false;
  return words.find(" " + w + " ") != std::string::npos ||
         words.find(" " + w + "s ") != std::string::npos;
}

// ------------------------------------------------------------- the extras

float fval(const char *s, float def) {
  char *end = nullptr;
  const float v = std::strtof(s, &end);
  return end == s ? def : v;
}

void pair_val(const char *s, float out[2]) {
  char *end = nullptr;
  const float a = std::strtof(s, &end);
  if (end == s) return;
  out[0] = a;
  if (*end == ',') out[1] = std::strtof(end + 1, nullptr);
}

void extra_token(const std::string &t, PlantDescription &d) {
  const size_t eq = t.find('=');
  if (eq == std::string::npos) {
    if (t == "evergreen") d.evergreen = true;
    else if (t == "deciduous") d.evergreen = false;
    else if (t == "flowers") d.flowers = true;
    else if (t == "fruits") d.fruits = true;
    else if (t == "opposite" || t == "alternate" || t == "whorled" || t == "spiral") d.arrangement = t;
    else if (t == "weeping") { d.droop = 0.85f; d.crown = "weeping"; }
    else if (t == "gnarled") { d.straightness = 0.35f; d.crown = "irregular"; }
    else if (t == "dense") { d.density = 0.9f; d.leaf_density = 0.95f; }
    else if (t == "sparse") { d.density = 0.35f; d.leaf_density = 0.45f; }
    return;
  }
  const std::string k = t.substr(0, eq);
  const char *v = t.c_str() + eq + 1;
  if (k == "angle") d.branch_angle_deg = fval(v, d.branch_angle_deg);
  else if (k == "levels") d.levels = (int)std::lround(fval(v, (float)d.levels));
  else if (k == "trunk") d.trunk_ratio = fval(v, d.trunk_ratio);
  else if (k == "density") d.density = fval(v, d.density);
  else if (k == "leafd") d.leaf_density = fval(v, d.leaf_density);
  else if (k == "droop") d.droop = fval(v, d.droop);
  else if (k == "straight") d.straightness = fval(v, d.straightness);
  else if (k == "width") d.width_ratio = fval(v, d.width_ratio);
  else if (k == "forks") d.forks = (int)std::lround(fval(v, (float)d.forks));
  else if (k == "cluster") d.leaf_cluster = v;
  else if (k == "crown") d.crown = v;
  else if (k == "fshape") { d.flower_shape = v; d.flowers = true; }
  else if (k == "fcolor") { hex_rgb((uint32_t)std::strtoul(v, nullptr, 16), d.flower_color); }
  else if (k == "fsize") d.flower_size_cm = fval(v, d.flower_size_cm);
  else if (k == "fseason") pair_val(v, d.flower_season);
  else if (k == "frcolor") { hex_rgb((uint32_t)std::strtoul(v, nullptr, 16), d.fruit_color); d.fruits = true; }
  else if (k == "frsize") d.fruit_size_cm = fval(v, d.fruit_size_cm);
  else if (k == "frseason") pair_val(v, d.fruit_season);
  else if (k == "age") d.age_years = fval(v, d.age_years);
  else if (k == "maxage") d.max_age_years = fval(v, d.max_age_years);
  else if (k == "var") d.variation = fval(v, d.variation);
  else if (k == "group") d.group = v;
}

void extras(const char *s, PlantDescription &d) {
  if (!s) return;
  std::string t;
  for (const char *p = s;; ++p) {
    if (*p && *p != ' ') {
      t += *p;
      continue;
    }
    if (!t.empty()) extra_token(t, d);
    t.clear();
    if (!*p) break;
  }
}

// One table row onto a description: what makes the plant itself, then its
// extras. Everything it does not mention keeps what the archetype gave.
void apply(const Plant &p, PlantDescription &d) {
  d.archetype = p.arch;
  d.group = p.group;
  d.height_m = p.height;
  d.crown = p.crown;
  d.bark = p.bark;
  hex_rgb(p.bark_rgb, d.bark_color);
  d.leaf_shape = p.leaf;
  d.leaf_size_cm = p.leaf_cm;
  hex_rgb(p.leaf_rgb, d.leaf_color);
  if (p.autumn_rgb) hex_rgb(p.autumn_rgb, d.autumn_color);
  else for (int i = 0; i < 3; ++i) d.autumn_color[i] = d.leaf_color[i];
  extras(p.extra, d);
  if (!p.name || !*p.name) return;
  d.name = p.name;
  if (p.latin && *p.latin) d.note = p.latin;
}

// ------------------------------------------------------- archetype rows
// The usual plant of each kind, in the same shape as a species row. A
// species is written on top of one of these, so a birch entry says only
// what a birch has that a broadleaf tree in general has not.

const std::vector<Plant> &arch_table() {
  static const std::vector<Plant> v = {
      {"broadleaf_tree", "Broadleaf tree", "", "broadleaf_tree", "trees", 15.f, "round", "fissured",
       0x4d4238, "ovate", 8.f, 0x3d7026, 0xb86a1a, "trunk=0.04 levels=4 angle=50 density=0.65 leafd=0.75 straight=0.75 width=0.9 age=40 maxage=200"},
      {"conifer", "Conifer", "", "conifer", "conifers", 25.f, "conical", "scaly", 0x53382a, "needle",
       4.f, 0x274d2b, 0, "evergreen whorled trunk=0.032 levels=3 angle=75 density=0.75 leafd=0.85 straight=0.95 width=0.45 frcolor=59402a frsize=6 frseason=0.5,0.95 age=60 maxage=300"},
      {"palm", "Palm", "", "palm", "palms", 12.f, "umbrella", "ringed", 0x736048, "frond", 180.f,
       0x336b23, 0, "evergreen cluster=frond trunk=0.02 levels=1 angle=55 density=0.5 leafd=0.9 droop=0.35 straight=0.85 width=0.55 age=30 maxage=90"},
      {"weeping_tree", "Weeping tree", "", "weeping_tree", "trees", 12.f, "weeping", "fissured",
       0x5a4a3a, "lanceolate", 9.f, 0x4d7a33, 0xc7a53a, "alternate trunk=0.035 levels=3 angle=65 density=0.7 leafd=0.85 droop=0.9 straight=0.6 width=1.1 age=35 maxage=120"},
      {"dead_tree", "Dead tree", "", "dead_tree", "trees", 10.f, "irregular", "fissured", 0x6b6155,
       "ovate", 6.f, 0x7a6b4a, 0x7a6b4a, "trunk=0.04 levels=3 angle=55 density=0.4 leafd=0 straight=0.4 var=0.45 age=120 maxage=130"},
      {"bonsai", "Bonsai", "", "bonsai", "trees", 0.45f, "irregular", "fissured", 0x4a3f33, "ovate",
       1.5f, 0x35662a, 0xc27a22, "gnarled trunk=0.12 levels=3 angle=70 density=0.8 leafd=0.9 width=1.3 var=0.4 age=40 maxage=200"},
      {"shrub", "Shrub", "", "shrub", "shrubs", 1.5f, "shrubby", "fibrous", 0x6b5a45, "ovate", 4.f,
       0x3d7026, 0xb8842a, "trunk=0.02 levels=2 angle=40 density=0.8 leafd=0.85 width=1.2 age=8 maxage=40"},
      {"fern", "Fern", "", "fern", "ferns", 0.7f, "shrubby", "fibrous", 0x5c4a33, "pinnate", 45.f,
       0x2e6124, 0x9c7d33, "cluster=frond trunk=0.01 levels=1 density=0.8 leafd=0.9 droop=0.4 width=1.3 var=0.35 age=4 maxage=25"},
      {"grass_tuft", "Grass tuft", "", "grass_tuft", "grasses", 0.6f, "shrubby", "fibrous", 0x6b6142,
       "blade", 40.f, 0x5c8229, 0xb8a352, "cluster=tuft trunk=0.004 levels=1 density=0.9 leafd=0.9 droop=0.4 width=1.2 var=0.4 age=2 maxage=8"},
      {"flowering_plant", "Flowering plant", "", "flowering_plant", "herbs", 0.6f, "round", "smooth",
       0x546b38, "ovate", 6.f, 0x42752b, 0xa38a33, "flowers fshape=disc fcolor=e04d59 fsize=4 fseason=0.25,0.6 trunk=0.01 levels=2 density=0.6 leafd=0.8 width=0.8 age=2 maxage=8"},
      {"cactus_columnar", "Columnar cactus", "", "cactus_columnar", "succulents", 3.f, "columnar",
       "fibrous", 0x43662f, "needle", 2.f, 0x43662f, 0, "evergreen flowers fshape=cup fcolor=f5f0e0 fsize=6 fseason=0.35,0.5 trunk=0.09 levels=2 angle=80 density=0.3 leafd=0 straight=0.95 width=0.35 age=40 maxage=150"},
      {"cactus_paddle", "Paddle cactus", "", "cactus_paddle", "succulents", 1.5f, "irregular",
       "smooth", 0x4d7534, "needle", 1.5f, 0x4d7534, 0, "evergreen flowers fshape=cup fcolor=e8c331 fsize=5 fseason=0.3,0.45 frcolor=a3243a frsize=4 trunk=0.12 levels=3 density=0.6 leafd=0 width=1.2 var=0.35 age=12 maxage=40"},
      {"succulent_rosette", "Succulent rosette", "", "succulent_rosette", "succulents", 0.4f,
       "shrubby", "smooth", 0x5c8759, "lanceolate", 25.f, 0x5c8759, 0, "evergreen cluster=rosette trunk=0.05 levels=1 density=0.9 leafd=0.9 width=1.4 age=6 maxage=30"},
      {"vine", "Vine", "", "vine", "climbers", 6.f, "irregular", "fibrous", 0x59452f, "heart", 8.f,
       0x336b26, 0xa8752a, "trunk=0.006 levels=2 angle=60 density=0.7 leafd=0.8 droop=0.5 straight=0.3 width=0.6 var=0.4 age=8 maxage=60"},
      {"bamboo", "Bamboo", "", "bamboo", "grasses", 8.f, "columnar", "smooth", 0x9c9c47,
       "lanceolate", 12.f, 0x4d8229, 0, "evergreen trunk=0.008 levels=2 angle=45 density=0.6 leafd=0.8 straight=0.98 width=0.3 age=6 maxage=30"},
      {"reed", "Reed", "", "reed", "grasses", 2.5f, "columnar", "fibrous", 0x7a7547, "linear", 40.f,
       0x5c8238, 0xbfae59, "cluster=tuft flowers fshape=spike fcolor=8a6b47 fsize=15 fseason=0.5,0.9 trunk=0.004 levels=1 density=0.8 leafd=0.85 straight=0.9 width=0.2 age=3 maxage=10"},
      {"mushroom", "Mushroom", "", "mushroom", "fungi", 0.12f, "umbrella", "smooth", 0xe0d6bf,
       "round", 1.f, 0xe0d6bf, 0, "fcolor=b8442b trunk=0.12 levels=1 density=0.3 leafd=0 width=1.2 var=0.4 age=1 maxage=2"},
      {"ground_cover", "Ground cover", "", "ground_cover", "herbs", 0.12f, "shrubby", "smooth",
       0x546b3d, "round", 3.f, 0x3d7526, 0x9c8233, "trunk=0.01 levels=1 density=0.9 leafd=0.9 width=3 var=0.4 age=2 maxage=10"}};
  return v;
}

// ------------------------------------------------------------- modifiers

void mix(float c[3], const float to[3], float t) {
  for (int i = 0; i < 3; ++i) c[i] += (to[i] - c[i]) * t;
}

bool is_tree(const std::string &a) {
  return a == "broadleaf_tree" || a == "conifer" || a == "weeping_tree" || a == "bonsai" ||
         a == "palm";
}

// The words around the name. `said` collects what was applied, for the note.
void modifiers(const std::string &w, PlantDescription &d, std::string &said) {
  auto add = [&said](const char *s) { said += said.empty() ? "" : ", "; said += s; };
  const float ash[3] = {0.45f, 0.42f, 0.38f};

  // size
  if (has_word(w, "giant") || has_word(w, "huge") || has_word(w, "massive")) {
    d.height_m *= 2.f; d.max_age_years *= 1.3f; add("unusually large");
  } else if (has_word(w, "tall")) { d.height_m *= 1.35f; add("tall"); }
  if (has_word(w, "dwarf") || has_word(w, "miniature") || has_word(w, "tiny")) {
    d.height_m *= 0.35f; d.leaf_size_cm *= 0.6f; add("dwarf");
  } else if (has_word(w, "short") || has_word(w, "small") || has_word(w, "low")) {
    d.height_m *= 0.6f; add("small");
  }
  // age
  if (has_word(w, "ancient")) {
    d.age_years = d.max_age_years * 0.95f;
    d.straightness *= 0.55f; d.trunk_ratio *= 1.4f; d.height_m *= 1.12f;
    d.variation = std::min(1.f, d.variation + 0.15f);
    d.crown = "irregular";
    if (d.forks < 1 && is_tree(d.archetype)) d.forks = 1;
    add("ancient");
  } else if (has_word(w, "old") || has_word(w, "mature") || has_word(w, "veteran")) {
    d.age_years = d.max_age_years * 0.75f;
    d.straightness *= 0.8f; d.height_m *= 1.08f;
    d.variation = std::min(1.f, d.variation + 0.1f);
    add("old");
  } else if (has_word(w, "sapling") || has_word(w, "seedling")) {
    d.age_years = std::max(1.f, d.max_age_years * 0.03f);
    d.height_m *= 0.22f; d.trunk_ratio *= 0.7f;
    d.levels = std::max(1, d.levels - 1);
    add("a sapling");
  } else if (has_word(w, "young")) {
    d.age_years = d.max_age_years * 0.2f;
    d.height_m *= 0.55f;
    d.levels = std::max(1, d.levels - 1);
    add("young");
  }
  // habit
  if (has_word(w, "weeping")) {
    d.droop = std::max(d.droop, 0.85f);
    d.crown = "weeping";
    if (d.archetype == "broadleaf_tree") d.archetype = "weeping_tree";
    add("weeping");
  }
  if (has_word(w, "twisted") || has_word(w, "gnarled") || has_word(w, "windswept") ||
      has_word(w, "crooked")) {
    d.straightness *= 0.35f;
    d.variation = std::min(1.f, d.variation + 0.2f);
    d.crown = "irregular";
    add("twisted by the weather");
  }
  if (has_word(w, "bushy")) {
    d.density = std::min(1.f, d.density * 1.3f + 0.1f);
    d.leaf_density = std::min(1.f, d.leaf_density * 1.2f + 0.1f);
    d.width_ratio *= 1.2f;
    if (d.height_m < 3.f) d.crown = "shrubby";
    add("bushy");
  }
  if (has_word(w, "dense") || has_word(w, "thick")) {
    d.density = std::min(1.f, d.density * 1.35f);
    d.leaf_density = std::min(1.f, d.leaf_density * 1.25f);
    add("densely grown");
  } else if (has_word(w, "sparse") || has_word(w, "thin") || has_word(w, "open")) {
    d.density *= 0.55f; d.leaf_density *= 0.55f;
    add("sparse");
  }
  // Health is a setting of the plant, not a colour baked into it: the parts'
  // presence-by-health curves thin the leaves and the health tint browns
  // them as it falls, so turning it back up makes the same plant well again.
  // Only what is truly the species' own - a snag is a different archetype,
  // and a dead tree keeps no leaves at all - is written into the shape.
  if (has_word(w, "dead")) {
    if (is_tree(d.archetype)) d.archetype = "dead_tree";
    d.health = 0.f;
    d.leaf_density = 0.f; d.flowers = false; d.fruits = false; d.evergreen = false;
    mix(d.bark_color, ash, 0.5f);
    add("dead");
  } else if (has_word(w, "dry") || has_word(w, "withered") || has_word(w, "dying") ||
             has_word(w, "parched")) {
    d.health = 0.25f;
    add("dry and failing");
  } else if (has_word(w, "thriving") || has_word(w, "lush") || has_word(w, "healthy") ||
             has_word(w, "vigorous")) {
    d.health = 1.f;
    d.leaf_density = std::min(1.f, d.leaf_density * 1.25f + 0.1f);
    d.density = std::min(1.f, d.density * 1.15f);
    add("in full health");
  }
  // The season, likewise: the year is where the plant is looked at, and the
  // presence-over-the-year curves and the seasonal tints do the rest.
  if (has_word(w, "winter")) {
    d.season = 0.f;
    add("seen in winter");
  } else if (has_word(w, "autumn") || has_word(w, "fall")) {
    d.season = 0.78f;
    add("seen in autumn");
  } else if (has_word(w, "spring")) {
    d.season = 0.25f;
    add("seen in spring");
  } else if (has_word(w, "summer")) {
    d.season = 0.5f;
    add("seen in midsummer");
  }
  d.height_m = std::clamp(d.height_m, 0.02f, 150.f);
  d.age_years = std::clamp(d.age_years, 0.f, d.max_age_years);
}

std::string titled(const std::string &s) {
  std::string out = s;
  if (!out.empty()) out[0] = (char)std::toupper((unsigned char)out[0]);
  return out;
}

} // namespace

// ---------------------------------------------------------------- tables

const std::vector<Plant> &table() {
  static const std::vector<Plant> v = [] {
    std::vector<Plant> out;
    table_trees(out);
    table_small(out);
    return out;
  }();
  return v;
}

void archetype_defaults(const std::string &arch, PlantDescription &d) {
  d = PlantDescription();
  for (const Plant &p : arch_table())
    if (arch == p.key) {
      apply(p, d);
      d.name = "Plant";
      d.note.clear();
      return;
    }
  d.archetype = arch.empty() ? std::string("broadleaf_tree") : arch;
}

std::string archetype_guess(const std::string &words) {
  const std::string w = clean(words);
  // the archetypes' own names first, spelt either way
  for (const Plant &p : arch_table()) {
    std::string spaced = p.key;
    for (char &c : spaced)
      if (c == '_') c = ' ';
    if (has_word(w, p.key) || has_word(w, spaced)) return p.key;
  }
  struct Hint { const char *word; const char *arch; };
  static const Hint HINTS[] = {
      {"prickly pear", "cactus_paddle"}, {"paddle cactus", "cactus_paddle"},
      {"opuntia", "cactus_paddle"},      {"barrel cactus", "cactus_columnar"},
      {"saguaro", "cactus_columnar"},    {"cactus", "cactus_columnar"},
      {"cacti", "cactus_columnar"},      {"succulent", "succulent_rosette"},
      {"agave", "succulent_rosette"},    {"aloe", "succulent_rosette"},
      {"rosette", "succulent_rosette"},  {"conifer", "conifer"},
      {"pine", "conifer"},               {"fir", "conifer"},
      {"spruce", "conifer"},             {"cedar", "conifer"},
      {"larch", "conifer"},              {"cypress", "conifer"},
      {"juniper", "conifer"},            {"yew", "conifer"},
      {"hemlock", "conifer"},            {"redwood", "conifer"},
      {"sequoia", "conifer"},            {"needles", "conifer"},
      {"palm", "palm"},                  {"bamboo", "bamboo"},
      {"bracken", "fern"},               {"fern", "fern"},
      {"cattail", "reed"},               {"bulrush", "reed"},
      {"papyrus", "reed"},               {"rush", "reed"},
      {"sedge", "reed"},                 {"reed", "reed"},
      {"toadstool", "mushroom"},         {"fungus", "mushroom"},
      {"fungi", "mushroom"},             {"mushroom", "mushroom"},
      {"moss", "ground_cover"},          {"creeping", "ground_cover"},
      {"groundcover", "ground_cover"},   {"turf", "grass_tuft"},
      {"lawn", "grass_tuft"},            {"grass", "grass_tuft"},
      {"creeper", "vine"},               {"climber", "vine"},
      {"liana", "vine"},                 {"ivy", "vine"},
      {"vine", "vine"},                  {"snag", "dead_tree"},
      {"deadwood", "dead_tree"},         {"driftwood", "dead_tree"},
      {"stump", "dead_tree"},            {"bonsai", "bonsai"},
      {"hedge", "shrub"},                {"bush", "shrub"},
      {"shrub", "shrub"},                {"wildflower", "flowering_plant"},
      {"blossom", "flowering_plant"},    {"flower", "flowering_plant"},
      {"herb", "flowering_plant"},       {"weed", "flowering_plant"},
      {"sapling", "broadleaf_tree"},     {"tree", "broadleaf_tree"},
      {"wood", "broadleaf_tree"}};
  for (const Hint &h : HINTS)
    if (has_word(w, h.word)) return h.arch;
  return "";
}

} // namespace know
} // namespace plant

// ------------------------------------------------------ words to a plant

bool plant_describe_from_words(const std::string &words, PlantDescription &out) {
  using namespace plant::know;
  const std::string w = clean(words);

  // The longest name that appears in the words wins: "japanese maple" must
  // never be answered by "maple", and "weeping willow" not by "willow".
  const Plant *best = nullptr;
  size_t best_len = 0;
  for (const Plant &p : table()) {
    std::string key;
    for (const char *c = p.key;; ++c) {
      if (*c && *c != '|') {
        key += *c;
        continue;
      }
      if (key.size() > best_len && has_word(w, key)) {
        best = &p;
        best_len = key.size();
      }
      key.clear();
      if (!*c) break;
    }
  }

  PlantDescription d;
  bool known = false;
  if (best) {
    archetype_defaults(best->arch, d);
    apply(*best, d);
    known = true;
  } else {
    const std::string arch = archetype_guess(words);
    archetype_defaults(arch.empty() ? "broadleaf_tree" : arch, d);
    // A bare archetype name ("conifer", "grass tuft") is something we know;
    // a word we merely read a hint from is a guess and says so.
    for (const std::string &a : plant_archetypes())
      if (d.archetype == a) {
        std::string spaced = a;
        for (char &c : spaced)
          if (c == '_') c = ' ';
        if (has_word(w, a) || has_word(w, spaced)) known = true;
      }
  }
  std::string said;
  modifiers(w, d, said);
  if (!said.empty()) {
    if (!d.note.empty() && d.note.back() != '.') d.note += '.';
    if (!d.note.empty()) d.note += ' ';
    d.note += titled(said) + ".";
  }
  // A plant we know keeps its own name ("Silver birch"), whatever was typed
  // around it; one we do not is called what the person called it.
  if (!best && !words.empty() && words.size() <= 48) d.name = titled(words);
  if (d.name.empty()) d.name = "Plant";
  out = d;
  return known;
}

} // namespace gpx
