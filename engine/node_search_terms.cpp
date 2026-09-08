// Geekatplay TerraForge - the concept table behind node search.
//
// A word in a query rarely matches the word a node happens to use. Someone
// looking for "rocks" wants FieldStones, FakeStones and Scree; someone
// looking for "wear down the mountains" wants Hydraulic and Thermal, neither
// of which contains any of those words. This table is what carries a query
// from what a person said to what the catalogue calls it.
//
// Each row is a set of words that mean nearly the same thing *for the purpose
// of finding a node*. Membership is symmetric: any word in a row expands to
// the whole row. Rows may overlap - "water" is in both the hydrology row and
// the erosion row, and should be.
//
// This is the honest limit of the search: it generalises exactly as far as
// this table does and no further. It is kept here, apart from the scoring, so
// that widening the vocabulary is a data change anyone can make and review
// without touching the maths.
#include <string>
#include <vector>

namespace gpx::search {

const std::vector<std::vector<std::string>> &concept_rows() {
  static const std::vector<std::vector<std::string>> R = {
      // --- ground material ------------------------------------------------
      {"rock", "rocks", "rocky", "stone", "stones", "stony", "boulder",
       "boulders", "scree", "talus", "gravel", "grit", "pebble", "pebbles",
       "cobble", "cobbles", "rubble", "shingle"},
      {"sand", "sandy", "dune", "dunes", "desert", "aeolian"},
      {"soil", "dirt", "earth", "ground", "mud", "clay", "sediment"},
      {"snow", "ice", "glacier", "glacial", "frost", "frozen"},
      {"grass", "grassy", "sward", "turf", "lawn", "meadow", "tuft", "tufts",
       "pasture", "tussock"},
      {"plant", "plants", "tree", "trees", "forest", "shrub", "bush",
       "vegetation", "flora", "foliage", "ecosystem", "scatter"},

      // --- landform ---------------------------------------------------------
      {"mountain", "mountains", "peak", "peaks", "summit", "alpine", "ridge",
       "ridges", "range"},
      {"valley", "canyon", "gorge", "ravine", "gully"},
      {"hill", "hills", "hilly", "mound", "knoll", "rolling"},
      {"plateau", "mesa", "butte", "tableland", "flat", "plain"},
      {"cliff", "escarpment", "bluff", "scarp", "steep", "slope"},
      {"crater", "caldera", "volcano", "volcanic"},
      {"island", "archipelago", "atoll"},
      {"coast", "coastal", "shore", "shoreline", "beach", "waterline"},

      // --- water ------------------------------------------------------------
      {"water", "lake", "lakes", "pond", "sea", "ocean", "hydro", "hydrology",
       "aquatic", "flood", "flooding", "submerged"},
      {"river", "rivers", "stream", "streams", "creek", "channel", "channels",
       "drainage", "tributary", "flow"},
      {"wet", "wetness", "moisture", "damp", "saturation"},

      // --- process ----------------------------------------------------------
      {"erode", "erodes", "eroded", "erosion", "weather", "weathering",
       "wear", "degrade", "denude"},
      {"deposit", "deposition", "sediment", "silt", "alluvial", "fan"},
      {"fold", "fault", "tectonic", "uplift", "strata", "stratum",
       "sedimentary", "geology", "geological"},
      {"melt", "thaw", "freeze", "thermal"},

      // --- pattern and maths ------------------------------------------------
      {"noise", "random", "fractal", "perlin", "worley", "voronoi", "cellular",
       "turbulence", "billow", "ridged"},
      {"blur", "smooth", "soften", "smoothing", "gauss", "gaussian"},
      {"sharp", "sharpen", "crisp", "detail", "contrast"},
      {"warp", "distort", "displace", "deform", "bend", "twist"},
      {"blend", "mix", "combine", "merge", "composite", "layer", "stack"},
      {"mask", "select", "selection", "isolate", "region", "area", "where"},
      {"invert", "flip", "reverse", "negate"},
      {"scale", "resize", "resample", "zoom", "size"},
      {"tile", "tiling", "repeat", "seamless", "quilt", "wrap"},

      // --- appearance -------------------------------------------------------
      {"color", "colour", "tint", "hue", "shade", "palette", "gradient",
       "ramp"},
      {"material", "texture", "shader", "surface", "pbr", "albedo",
       "roughness", "metallic", "normal"},
      {"light", "lighting", "lamp", "sun", "sunlight", "illumination",
       "shadow"},
      {"sky", "atmosphere", "haze", "fog", "mist", "air"},
      {"cloud", "clouds", "cumulus", "overcast"},

      // --- camera and output ------------------------------------------------
      {"camera", "lens", "focal", "view", "shot", "framing"},
      {"render", "output", "export", "save", "write", "bake"},
      {"animate", "animation", "keyframe", "timeline", "motion", "time"},

      // --- shape ------------------------------------------------------------
      {"shape", "outline", "form", "silhouette", "boundary", "edge",
       "border"},
      {"circle", "round", "disc", "ellipse", "sphere"},
      {"square", "rectangle", "box", "rect"},
      {"path", "road", "trail", "route", "spline", "curve", "line"},
      // "cloud" is deliberately NOT here. A point cloud is a cloud only to a
      // programmer; to everyone else the word means the thing in the sky, and
      // putting it in this row made "clouds at sunset" return the scatter
      // nodes. "point cloud" still finds them, through "point".
      {"point", "points", "instance", "instances", "placement", "scatter",
       "distribute", "distribution", "population"},

      // --- doing it by hand -------------------------------------------------
      // The whole vocabulary of hand work was missing, so "paint" found the
      // mask painter and ranked the node that actually paints terrain height
      // fourth, at a score of 0.04 - invisible. "draw" found nothing at all.
      // Someone reaching for a brush does not care whether the thing they are
      // about to paint is called a sculpt, a mask or a layer.
      {"paint", "painting", "painted", "painter", "draw", "drawing", "drawn",
       "brush", "brushes", "brushed", "sculpt", "sculpting", "sculpted",
       "stroke", "strokes", "canvas", "freehand", "handmade", "manual",
       "retouch", "touchup", "erase", "eraser"},
      // What a hand-painted layer *is*, so "greyscale height map" reaches it
      // as well as the file readers.
      {"heightmap", "heightfield", "greyscale", "grayscale", "bitmap",
       "picture", "image", "photo"},

      // --- how much / how strong -------------------------------------------
      {"height", "elevation", "altitude", "tall", "depth", "relief"},
      {"rough", "roughness", "bumpy", "coarse", "jagged"},
      {"flat", "level", "even", "smooth", "plane"},
  };
  return R;
}

// Words that carry no signal in a query about nodes. Kept short on purpose:
// dropping a word that turns out to matter is worse than scoring a common one
// low, and the IDF already discounts anything that appears everywhere.
const std::vector<std::string> &stop_words() {
  static const std::vector<std::string> S = {
      "a",    "an",   "and",  "are",  "as",   "at",   "be",   "by",   "can",
      "do",   "for",  "from", "get",  "how",  "i",    "in",   "is",   "it",
      "make", "me",   "my",   "of",   "on",   "or",   "that", "the",  "then",
      "this", "to",   "up",   "want", "was",  "what", "when", "with", "you"};
  return S;
}

} // namespace gpx::search
