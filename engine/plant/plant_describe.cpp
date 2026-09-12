// Geekatplay TerraForge - a plant described in words a machine can read.
//
// Between "an old olive tree" and a graph of forty nodes sits one flat
// record: PlantDescription, thirty-odd numbers and words that say what the
// plant is - how tall, what crown, what bark, what leaf, whether it turns
// in autumn, when it flowers. The archetype builders read only that, so
// everything that can describe a plant (the knowledge table, a language
// model, a person typing into the editor, a saved species' record) meets
// the builders at the same place and none of them has to know about nodes.
//
// This file is the record's two faces: its JSON, and the schema text a
// model is given as its instructions. The reader is deliberately forgiving
// - a model that answers with a colour as "#3f6b26" instead of three
// numbers, or an age as a string, or half the fields missing, has still
// described a plant, and refusing it would only make the plant editor
// brittle. The one thing it will not guess at is the archetype: a
// structure we cannot grow is an error, not a default, though a word that
// plainly suggests one ("a pine", "tree") is taken.
//
// Anything left out takes the archetype's usual value rather than a bare
// zero, so {"archetype":"conifer","height_m":30} grows a thirty-metre
// conifer and not a thirty-metre stick.
#include "gpx/plant.hpp"
#include "plant/plant_knowledge.hpp"
#include <algorithm>
#include <cctype>
#include <exception>
#include <json.hpp>
#include <string>
#include <vector>

namespace gpx {
namespace {

using nlohmann::json;

std::string lower(std::string s) {
  for (char &c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

// ------------------------------------------------------------- reading

// A number, however it arrives: 12, 12.5, "12.5".
bool number(const json &j, float &out) {
  if (j.is_number()) {
    out = j.get<float>();
    return true;
  }
  if (!j.is_string()) return false;
  try {
    out = std::stof(j.get<std::string>());
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

void getf(const json &j, const char *key, float &out, float lo, float hi) {
  float v = 0.f;
  if (j.contains(key) && number(j[key], v)) out = std::clamp(v, lo, hi);
}
void geti(const json &j, const char *key, int &out, int lo, int hi) {
  float v = 0.f;
  if (j.contains(key) && number(j[key], v)) out = std::clamp((int)std::lround(v), lo, hi);
}
void getb(const json &j, const char *key, bool &out) {
  if (!j.contains(key)) return;
  const json &v = j[key];
  if (v.is_boolean()) out = v.get<bool>();
  else if (v.is_number()) out = v.get<float>() != 0.f;
  else if (v.is_string()) {
    const std::string s = lower(v.get<std::string>());
    out = s == "true" || s == "yes" || s == "1";
  }
}
void gets(const json &j, const char *key, std::string &out) {
  if (j.contains(key) && j[key].is_string()) out = j[key].get<std::string>();
}
// A word from a list; anything else keeps what was there, because a model
// inventing "semi-weeping" should not cost us the rest of the plant.
void getword(const json &j, const char *key, std::string &out, const std::vector<const char *> &ok) {
  if (!j.contains(key) || !j[key].is_string()) return;
  const std::string w = lower(j[key].get<std::string>());
  for (const char *c : ok)
    if (w == c) {
      out = w;
      return;
    }
}

// [r,g,b] in 0..1 or in 0..255, "#rrggbb", or {"r":..,"g":..,"b":..}.
void getcolor(const json &j, const char *key, float out[3]) {
  if (!j.contains(key)) return;
  const json &v = j[key];
  float c[3] = {out[0], out[1], out[2]};
  if (v.is_array() && v.size() >= 3) {
    bool ok = true;
    for (int i = 0; i < 3; ++i) ok = number(v[(size_t)i], c[i]) && ok;
    if (!ok) return;
    if (c[0] > 1.5f || c[1] > 1.5f || c[2] > 1.5f)
      for (float &x : c) x /= 255.f;
  } else if (v.is_object()) {
    if (!v.contains("r") || !v.contains("g") || !v.contains("b")) return;
    if (!number(v["r"], c[0]) || !number(v["g"], c[1]) || !number(v["b"], c[2])) return;
    if (c[0] > 1.5f || c[1] > 1.5f || c[2] > 1.5f)
      for (float &x : c) x /= 255.f;
  } else if (v.is_string()) {
    std::string s = v.get<std::string>();
    if (!s.empty() && s[0] == '#') s.erase(0, 1);
    if (s.size() != 6) return;
    for (int i = 0; i < 3; ++i) {
      const std::string byte = s.substr((size_t)i * 2, 2);
      try {
        c[i] = (float)std::stoi(byte, nullptr, 16) / 255.f;
      } catch (const std::exception &) {
        return;
      }
    }
  } else {
    return;
  }
  for (int i = 0; i < 3; ++i) out[i] = std::clamp(c[i], 0.f, 1.f);
}

// [from, to] through the year, or {"from":..,"to":..}.
void getseason(const json &j, const char *key, float out[2]) {
  if (!j.contains(key)) return;
  const json &v = j[key];
  float a = out[0], b = out[1];
  if (v.is_array() && v.size() >= 2) {
    if (!number(v[0], a) || !number(v[1], b)) return;
  } else if (v.is_object()) {
    if (!v.contains("from") || !v.contains("to")) return;
    if (!number(v["from"], a) || !number(v["to"], b)) return;
  } else {
    return;
  }
  out[0] = std::clamp(a, 0.f, 1.f);
  out[1] = std::clamp(b, 0.f, 1.f);
}

const std::vector<const char *> CROWNS = {"round",   "spreading", "columnar", "conical", "weeping",
                                          "vase",    "irregular", "umbrella", "shrubby"};
const std::vector<const char *> BARKS = {"fissured", "plated",  "smooth", "birch",
                                         "ringed",   "peeling", "scaly",  "fibrous"};
const std::vector<const char *> ARRANGE = {"alternate", "opposite", "whorled", "spiral"};
const std::vector<const char *> LEAVES = {"ovate", "lanceolate", "lobed",    "palmate", "needle",
                                          "scale", "pinnate",    "heart",    "linear",  "round",
                                          "elliptic", "frond",    "blade"};
const std::vector<const char *> CLUSTERS = {"single", "pairs", "rosette", "frond", "tuft"};
const std::vector<const char *> FLOWERS = {"disc", "cup", "tube", "bell", "spike", "cluster"};

} // namespace

// ----------------------------------------------------------- archetypes

const std::vector<std::string> &plant_archetypes() {
  // Menu order: the big things first, then what grows under them. A name is
  // permanent once shipped - species files and the assistant's answers name
  // them - so an archetype is added to the end, never renamed.
  static const std::vector<std::string> v = {
      "broadleaf_tree",  "conifer",       "palm",              "weeping_tree", "dead_tree",
      "bonsai",          "shrub",         "fern",              "grass_tuft",   "flowering_plant",
      "cactus_columnar", "cactus_paddle", "succulent_rosette", "vine",         "bamboo",
      "reed",            "mushroom",      "ground_cover"};
  return v;
}

// ----------------------------------------------------------------- JSON

std::string plant_description_json(const PlantDescription &d) {
  json j;
  j["name"] = d.name;
  j["archetype"] = d.archetype;
  j["group"] = d.group;
  j["height_m"] = d.height_m;
  j["width_ratio"] = d.width_ratio;
  j["crown"] = d.crown;
  j["bark"] = d.bark;
  j["bark_color"] = {d.bark_color[0], d.bark_color[1], d.bark_color[2]};
  j["trunk_ratio"] = d.trunk_ratio;
  j["straightness"] = d.straightness;
  j["forks"] = d.forks;
  j["levels"] = d.levels;
  j["density"] = d.density;
  j["branch_angle_deg"] = d.branch_angle_deg;
  j["droop"] = d.droop;
  j["arrangement"] = d.arrangement;
  j["leaf_shape"] = d.leaf_shape;
  j["leaf_size_cm"] = d.leaf_size_cm;
  j["leaf_color"] = {d.leaf_color[0], d.leaf_color[1], d.leaf_color[2]};
  j["autumn_color"] = {d.autumn_color[0], d.autumn_color[1], d.autumn_color[2]};
  j["evergreen"] = d.evergreen;
  j["leaf_density"] = d.leaf_density;
  j["leaf_cluster"] = d.leaf_cluster;
  j["flowers"] = d.flowers;
  j["flower_shape"] = d.flower_shape;
  j["flower_color"] = {d.flower_color[0], d.flower_color[1], d.flower_color[2]};
  j["flower_size_cm"] = d.flower_size_cm;
  j["flower_season"] = {d.flower_season[0], d.flower_season[1]};
  j["fruits"] = d.fruits;
  j["fruit_color"] = {d.fruit_color[0], d.fruit_color[1], d.fruit_color[2]};
  j["fruit_size_cm"] = d.fruit_size_cm;
  j["fruit_season"] = {d.fruit_season[0], d.fruit_season[1]};
  j["variation"] = d.variation;
  j["age_years"] = d.age_years;
  j["max_age_years"] = d.max_age_years;
  j["health"] = d.health;
  j["season"] = d.season;
  j["note"] = d.note;
  return j.dump(1);
}

bool plant_description_parse(const std::string &json_text, PlantDescription &out, std::string &err) {
  err.clear();
  const json j = json::parse(json_text, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    err = "the description is not a JSON object";
    return false;
  }
  try {
    // The archetype decides what every missing field means, so it is read
    // first and the rest is laid over its defaults.
    PlantDescription d;
    std::string arch;
    gets(j, "archetype", arch);
    arch = lower(arch);
    for (char &c : arch)
      if (c == ' ' || c == '-') c = '_';
    const std::vector<std::string> &known = plant_archetypes();
    if (!arch.empty() && std::find(known.begin(), known.end(), arch) == known.end()) {
      const std::string guess = plant::know::archetype_guess(arch);
      if (guess.empty()) {
        err = "no plant archetype called '" + arch + "'";
        return false;
      }
      arch = guess;
    }
    if (arch.empty()) {
      // No archetype named: the name and the note usually say what it is.
      std::string name, note;
      gets(j, "name", name);
      gets(j, "note", note);
      const std::string guess = plant::know::archetype_guess(name + " " + note);
      arch = guess.empty() ? std::string("broadleaf_tree") : guess;
    }
    plant::know::archetype_defaults(arch, d);
    d.archetype = arch;

    gets(j, "name", d.name);
    gets(j, "group", d.group);
    gets(j, "note", d.note);
    getf(j, "height_m", d.height_m, 0.01f, 150.f);
    getf(j, "width_ratio", d.width_ratio, 0.05f, 4.f);
    getword(j, "crown", d.crown, CROWNS);
    getword(j, "bark", d.bark, BARKS);
    getcolor(j, "bark_color", d.bark_color);
    getf(j, "trunk_ratio", d.trunk_ratio, 0.002f, 0.5f);
    getf(j, "straightness", d.straightness, 0.f, 1.f);
    geti(j, "forks", d.forks, 0, 8);
    geti(j, "levels", d.levels, 1, 6);
    getf(j, "density", d.density, 0.f, 1.f);
    getf(j, "branch_angle_deg", d.branch_angle_deg, 0.f, 90.f);
    getf(j, "droop", d.droop, -1.f, 1.f);
    getword(j, "arrangement", d.arrangement, ARRANGE);
    getword(j, "leaf_shape", d.leaf_shape, LEAVES);
    getf(j, "leaf_size_cm", d.leaf_size_cm, 0.05f, 2000.f);
    getcolor(j, "leaf_color", d.leaf_color);
    getcolor(j, "autumn_color", d.autumn_color);
    getb(j, "evergreen", d.evergreen);
    getf(j, "leaf_density", d.leaf_density, 0.f, 1.f);
    getword(j, "leaf_cluster", d.leaf_cluster, CLUSTERS);
    getb(j, "flowers", d.flowers);
    getword(j, "flower_shape", d.flower_shape, FLOWERS);
    getcolor(j, "flower_color", d.flower_color);
    getf(j, "flower_size_cm", d.flower_size_cm, 0.05f, 300.f);
    getseason(j, "flower_season", d.flower_season);
    getb(j, "fruits", d.fruits);
    getcolor(j, "fruit_color", d.fruit_color);
    getf(j, "fruit_size_cm", d.fruit_size_cm, 0.05f, 300.f);
    getseason(j, "fruit_season", d.fruit_season);
    getf(j, "variation", d.variation, 0.f, 1.f);
    getf(j, "max_age_years", d.max_age_years, 1.f, 5000.f);
    getf(j, "age_years", d.age_years, 0.f, 5000.f);
    if (d.age_years > d.max_age_years) d.age_years = d.max_age_years;
    getf(j, "health", d.health, 0.f, 1.f);
    getf(j, "season", d.season, 0.f, 1.f);
    if (d.name.empty()) d.name = "Plant";
    out = d;
    return true;
  } catch (const std::exception &e) {
    err = std::string("the description could not be read: ") + e.what();
    return false;
  }
}

// --------------------------------------------------------------- schema

std::string plant_description_schema() {
  std::string s =
      R"DOC(The JSON object describes one plant species. Every field may be left out;
what you leave out takes the archetype's usual value. Lengths are metres
(height_m) or centimetres (leaf_size_cm, flower_size_cm, fruit_size_cm);
colours are three numbers 0..1 or a "#rrggbb" string; a season is a pair
[from, to] through the year where 0 is midwinter, 0.25 spring, 0.5
midsummer, 0.75 autumn.

  name              text. What to call the species, e.g. "English oak".
  archetype         one of the list below. The structure the plant is grown
                    from. Required in practice: choose the one whose habit
                    matches, not the one whose name matches.
  group             text. The library shelf: trees, conifers, palms, shrubs,
                    herbs, grasses, succulents, ferns, climbers, fungi.
  height_m          mature height of one individual, 0.01..150.
  width_ratio       crown width / height, 0.05..4. A columnar poplar is 0.25,
                    an open oak 1.2.
  crown             round | spreading | columnar | conical | weeping | vase |
                    irregular | umbrella | shrubby. Shapes the branch lengths
                    up the trunk.
  bark              fissured | plated | smooth | birch | ringed | peeling |
                    scaly | fibrous. Picks the bark picture made from rules.
  bark_color        the trunk's colour in daylight.
  trunk_ratio       trunk radius / height, 0.002..0.5. A pine is 0.03, an
                    oak 0.045, a baobab 0.2, a herb 0.005.
  straightness      0 gnarled and leaning .. 1 ruler-straight.
  forks             0..8 trunks splitting near the base. 0 for a single stem.
  levels            1..6 generations of branching: a herb 1, a shrub 2, a
                    mature tree 4.
  density           0..1 how many branches leave each parent.
  branch_angle_deg  0..90 from the parent's axis. A poplar 20, an oak 55, a
                    spruce 75.
  droop             -1 branches rising .. 0 level .. 1 weeping.
  arrangement       alternate | opposite | whorled | spiral. How children sit
                    around the parent. Maples and ashes are opposite; most
                    trees are spiral.
  leaf_shape        ovate | lanceolate | lobed | palmate | needle | scale |
                    pinnate | heart | linear | round | elliptic | frond |
                    blade. Drawn as the leaf's picture and its outline.
  leaf_size_cm      length of one leaf (or one needle, or one frond).
  leaf_color        the leaf in summer.
  autumn_color      what it turns to before it falls. Ignored when evergreen.
  evergreen         true keeps the leaves all year; false drops them.
  leaf_density      0..1 how thickly the foliage sits.
  leaf_cluster      single | pairs | rosette | frond | tuft.
  flowers           true grows flowers.
  flower_shape      disc | cup | tube | bell | spike | cluster.
  flower_color      flower_size_cm      flower_season [from, to]
  fruits            true grows fruit, berries, cones or nuts.
  fruit_color       fruit_size_cm       fruit_season [from, to]
  variation         0..1 how much one individual differs from the next.
  age_years         the age of the individual to grow.
  max_age_years     what the species lives to. The plant is grown at
                    age_years / max_age_years of its full size.
  health            0 dead, 0.3 dry and failing, 1 thriving. A setting of
                    the plant, not a colour: the leaves brown and thin by
                    themselves, and turning it back up makes it well again.
  season            when it is seen: 0 midwinter, 0.25 spring, 0.5
                    midsummer, 0.75 autumn. Also a setting, so the same
                    plant can be taken round the year.
  note              one sentence of prose. Put the botanical name here.

Archetypes:
)DOC";
  for (const std::string &a : plant_archetypes()) s += "  " + a + "\n";
  s +=
      R"DOC(
Three worked examples.

An old oak in a field:
{"name":"English oak","archetype":"broadleaf_tree","group":"trees","height_m":22,
 "width_ratio":1.2,"crown":"spreading","bark":"fissured","bark_color":[0.28,0.24,0.2],
 "trunk_ratio":0.045,"straightness":0.6,"forks":0,"levels":4,"density":0.7,
 "branch_angle_deg":55,"droop":0.1,"arrangement":"spiral","leaf_shape":"lobed",
 "leaf_size_cm":11,"leaf_color":[0.22,0.4,0.13],"autumn_color":[0.62,0.42,0.12],
 "evergreen":false,"leaf_density":0.8,"leaf_cluster":"single","flowers":false,
 "fruits":true,"fruit_color":[0.45,0.35,0.12],"fruit_size_cm":2.5,
 "fruit_season":[0.6,0.85],"variation":0.35,"age_years":140,"max_age_years":600,
 "note":"Quercus robur, the pedunculate oak of European hedgerows."}

A saguaro in flower:
{"name":"Saguaro","archetype":"cactus_columnar","group":"succulents","height_m":12,
 "width_ratio":0.35,"crown":"columnar","bark":"fibrous","bark_color":[0.2,0.35,0.18],
 "trunk_ratio":0.09,"straightness":0.95,"levels":2,"density":0.3,
 "branch_angle_deg":80,"leaf_shape":"needle","leaf_size_cm":2,
 "leaf_color":[0.24,0.42,0.2],"evergreen":true,"leaf_density":0,"flowers":true,
 "flower_shape":"cup","flower_color":[0.96,0.94,0.88],"flower_size_cm":8,
 "flower_season":[0.35,0.45],"variation":0.3,"age_years":80,"max_age_years":180,
 "note":"Carnegiea gigantea; arms only appear after about seventy years."}

A lavender bush:
{"name":"Lavender","archetype":"shrub","group":"herbs","height_m":0.6,
 "width_ratio":1.3,"crown":"shrubby","bark":"fibrous","bark_color":[0.42,0.38,0.3],
 "trunk_ratio":0.012,"straightness":0.8,"levels":2,"density":0.9,
 "branch_angle_deg":25,"droop":-0.1,"arrangement":"opposite","leaf_shape":"linear",
 "leaf_size_cm":3,"leaf_color":[0.45,0.5,0.38],"autumn_color":[0.45,0.5,0.38],
 "evergreen":true,"leaf_density":0.8,"leaf_cluster":"pairs","flowers":true,
 "flower_shape":"spike","flower_color":[0.45,0.35,0.7],"flower_size_cm":6,
 "flower_season":[0.4,0.65],"fruits":false,"variation":0.25,"age_years":6,
 "max_age_years":20,"note":"Lavandula angustifolia, a Mediterranean sub-shrub."}
)DOC";
  return s;
}

} // namespace gpx
