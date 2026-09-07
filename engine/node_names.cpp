// Geekatplay TerraForge - what a node is called on screen.
//
// A node type is a C++ identifier and reads like one. The node editor showed
// it directly, so the library offered "AOFromHeight", "PBRMaterial",
// "TerrainFractal2" and "PathSDF" - which are names for a compiler, not for
// the person deciding which node to reach for.
//
// The type stays exactly as it is: it is written into every saved project and
// the regression lock forbids removing or renaming one. Only the label
// changes, so this costs nothing and breaks nothing.
//
// Two mechanisms, in order:
//
//   1. Splitting the case, which handles most of them. "FieldColorMix" ->
//      "Field color mix". It knows about acronyms, because the naive rule
//      turns "AOFromHeight" into "AOFrom Height" and "PBRMaterial" into
//      "PBRMaterial".
//   2. An override, where splitting is not enough - a name that is jargon
//      however it is spaced ("SDF"), a version suffix that means nothing to
//      a user ("2"), or simply a better way of saying it.
//
// Sentence case throughout, because that is what the parameter labels use:
// of 935 multi-word labels in the registry, 809 are sentence case.
#include "gpx/node_graph.hpp"
#include <cctype>
#include <map>
#include <mutex>
#include <string>

namespace gpx {

namespace {

// Acronyms that must survive as a unit. Without this the case splitter cuts
// inside them: "AO|From|Height", "PBR|Material", "HSV" -> "HSV" only by luck.
const char *const ACRONYMS[] = {"PBR", "HSV", "RGB", "AO",  "SDF", "CSV",
                                "DEM", "LOD", "UV",  "EXR", "HDR", "FBX",
                                "OBJ", "PNG", "GPU", "CPU", "AI",  nullptr};

bool acronym_at(const std::string &s, size_t i, size_t &len) {
  for (int k = 0; ACRONYMS[k]; ++k) {
    const std::string a = ACRONYMS[k];
    if (s.compare(i, a.size(), a) != 0) continue;
    // It is only an acronym if what follows is not another lower-case letter
    // of the same word: "AOFrom" yes, "Aofrom" no.
    const size_t after = i + a.size();
    if (after < s.size() && std::islower((unsigned char)s[after])) continue;
    len = a.size();
    return true;
  }
  return false;
}

// "FieldColorMix" -> "Field color mix"; "AOFromHeight" -> "AO from height".
std::string split_case(const std::string &t) {
  std::string out;
  bool first_word = true;
  for (size_t i = 0; i < t.size();) {
    size_t alen = 0;
    if (acronym_at(t, i, alen)) {
      if (!out.empty()) out += ' ';
      out += t.substr(i, alen); // acronyms stay upper, wherever they fall
      i += alen;
      first_word = false;
      continue;
    }
    // a word runs from a capital (or the start) to the next capital
    size_t j = i + 1;
    while (j < t.size() && !std::isupper((unsigned char)t[j])) {
      size_t tmp = 0;
      if (acronym_at(t, j, tmp)) break;
      ++j;
    }
    std::string w = t.substr(i, j - i);
    if (!out.empty()) out += ' ';
    if (first_word) {
      if (!w.empty()) w[0] = (char)std::toupper((unsigned char)w[0]);
      first_word = false;
    } else {
      for (char &c : w) c = (char)std::tolower((unsigned char)c);
    }
    out += w;
    i = j;
  }
  return out;
}

// Where splitting the case is not enough. Keep this sorted by type so it
// reads as a list of decisions rather than a pile.
const std::map<std::string, std::string> &overrides() {
  static const std::map<std::string, std::string> M = {
      // Acronyms and jargon a user should not have to know.
      {"AOFromHeight", "Ambient occlusion from height"},
      {"AlbedoToPBR", "Full material from a photo"},
      {"PBRMaterial", "PBR material"},
      {"PathSDF", "Distance from a path"},
      {"PointsSDF", "Distance from points"},
      {"PointsFromCsv", "Points from a CSV file"},
      {"FieldTexCoord", "Field texture coordinates"},
      {"FieldToTexCoord", "Field to texture coordinates"},
      {"FieldTexCoordCombine", "Combine texture coordinates"},
      {"FieldTexCoordSplit", "Split texture coordinates"},
      {"FieldVectorOp", "Field vector maths"},

      // A version number is not a name.
      {"TerrainFractal2", "Rock and soil terrain"},
      {"Fractal", "Fractal terrain"},

      // Names that say what the node is rather than what it does.
      {"FakeStones", "Stones in the heightmap"},
      {"FieldStones", "Stone field"},
      {"FieldGrass", "Grass sward"},
      {"Splatmap", "Split a mask into channels"},
      {"SplatMaterial", "Blend materials by channel"},
      {"KMeans", "Group into regions"},
      {"Quilt", "Tile a pattern seamlessly"},
      {"Stratify", "Sedimentary strata"},
      {"Coast", "Beaches and sea cliffs"},
      {"Flood", "Standing water"},
      {"FillBasins", "Fill closed basins"},
      {"StreamPower", "River incision"},
      {"Hydraulic", "Water erosion"},
      {"Thermal", "Scree and talus"},
      {"Wind", "Wind erosion"},
      {"Dissolve", "Dissolution and karst"},
      {"Glaciation", "Glacial carving"},
      {"Peaks", "Sharpen the summits"},
      {"Grit", "Fine surface grit"},
      {"Cracks", "Cracks and fissures"},
      {"Plateau", "Flatten to plateaux"},
      {"Terrace", "Terraces and steps"},
      {"Fold", "Fold the range back"},
      {"ExpandShrink", "Grow or shrink a mask"},
      {"AreaRemove", "Remove small patches"},
      {"Skeleton", "Reduce to a centre line"},
      {"SkeletonDistance", "Distance from a centre line"},
      {"Morphology", "Open and close a mask"},
      {"RelativeElevation", "Height above the surroundings"},
      {"TerrainMetrics", "Measure the terrain"},
      {"Resample", "Change the sampling"},
      {"MetaNode", "Grouped subgraph"},
  };
  return M;
}

} // namespace

const std::string &node_display_name(const std::string &type) {
  // Cached: the node editor asks for this once per node per frame, and
  // splitting a string on every one of them every frame is work nobody
  // needs. Guarded because the studio's panels and the evaluator's worker
  // threads can both reach a node's label.
  static std::map<std::string, std::string> cache;
  static std::mutex m;
  std::lock_guard<std::mutex> lock(m);
  auto it = cache.find(type);
  if (it != cache.end()) return it->second;
  const auto &ov = overrides();
  auto o = ov.find(type);
  return cache.emplace(type, o != ov.end() ? o->second : split_case(type))
      .first->second;
}

} // namespace gpx
