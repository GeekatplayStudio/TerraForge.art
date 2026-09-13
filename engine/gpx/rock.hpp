// Geekatplay TerraForge - rocks, grown rather than collected.
//
// A landscape without stone reads as a lawn. The plant system already grows a
// species from a seed rather than loading a model, and stone wants the same
// treatment for the same reasons: a library of twenty boulders repeats
// visibly the moment you place two hundred, and a bought rock is somebody
// else's rock at somebody else's scale.
//
// So a rock is built from what actually shaped it. Three processes account
// for nearly every stone anyone has ever picked up:
//
//   FRACTURE  - rock breaks along joints and bedding planes, which are flat.
//               Cutting a lump with a few planes is not an approximation of
//               that; it is the thing itself, and it is why fresh scree is
//               angular and why its faces are flat rather than merely
//               bumpy.
//   ABRASION  - a stone carried by water or ice is worn at its corners first,
//               because that is where the stress is. Rounding is therefore
//               not a smoothing of the whole surface but a retreat of the
//               high-curvature parts, which is what makes a river cobble
//               ellipsoidal with the fracture facets still faintly readable.
//   WEATHERING- frost, salt and wind take the surface apart at several scales
//               at once, leaving pits, flutes and a grain that is fractal
//               rather than periodic.
//
// A type is a recipe over those three, not a different program. That is what
// makes the set extensible: a new rock is a new set of numbers.
//
// Everything is deterministic in `seed`: the same seed is the same stone, on
// any machine, which is what lets a scatter store an index instead of a mesh.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gpx {

// How a stone was made. The order is the order they are offered.
enum class RockType : int {
  Angular = 0,   // joint-bounded blocks, freshly broken: quarry, fresh scree
  Sharp,         // frost-shattered splinters, thin and edged: alpine scree
  River,         // tumbled and abraded: cobbles, bars, beds
  Boulder,       // large, subrounded, long-weathered: glacial erratics, moors
  Slab,          // bedding-parallel plates: sandstone, shale, flagstone
  Columnar,      // a cooling-joint column, five or six sided: basalt
  Weathered,     // pitted and fluted, tafoni and honeycomb: desert, coast
  Volcanic,      // vesicular, full of gas bubbles: scoria, pumice
  Outcrop,       // bedrock standing out of the ground, flat-bottomed
  Pebble,        // small and thoroughly rounded: shingle, gravel
  Count
};

// Every type by name, for menus, the schema and the assistant.
const char *rock_type_name(RockType t);
// The name people would type, matched loosely ("river stone", "sharp",
// "basalt column"). RockType::Count when nothing matches.
RockType rock_type_from_words(const std::string &words);
// One sentence on what made this kind of rock, for the tooltip and the
// assistant. Never empty.
const char *rock_type_note(RockType t);

struct RockParams {
  RockType type = RockType::Angular;
  uint32_t seed = 1;
  // The longest axis, metres. Everything else is proportional to it, so a
  // pebble and a boulder of the same type are the same shape at two sizes -
  // which is true of fracture and nearly true of abrasion.
  float size_m = 1.f;
  // How many triangles to spend. 0 is the type's own sensible amount; the
  // level is subdivisions of an icosahedron, so 2 is 320 faces and 4 is 5120.
  int detail = 0;
  // Dials over the recipe, each multiplying what the type asks for. 1 leaves
  // the type as it is; they are how a person makes their own rock without
  // needing a new type.
  float fracture = 1.f;   // how planar and how many the broken faces are
  float roundness = 1.f;  // how far abrasion has taken the corners
  float weathering = 1.f; // pits, flutes and surface grain
  float flatness = 1.f;   // squashed along one axis: bedding, or a slab
  float elongation = 1.f; // stretched along another: a splinter, a column
};

// A rock, ready to draw or export. Triangles, no strips; normals are per
// vertex and already smoothed within a face but not across a fracture edge,
// because a broken face has an edge and pretending otherwise is most of what
// makes a procedural rock look like a potato.
struct RockMesh {
  std::vector<float> pos;    // 3 per vertex
  std::vector<float> nrm;    // 3 per vertex
  std::vector<float> uv;     // 2 per vertex
  std::vector<uint32_t> idx; // 3 per triangle
  float bmin[3] = {0, 0, 0}, bmax[3] = {0, 0, 0};
  float height_m = 1.f;      // as built, so a caller can scale to a wanted size
  int vert_count() const { return (int)(pos.size() / 3); }
  int tri_count() const { return (int)(idx.size() / 3); }
};

// Build one. Deterministic in `p.seed` and in nothing else.
void rock_build(const RockParams &p, RockMesh &out);

// The recipe a type stands for, before the dials multiply it. Exposed so the
// tests can assert that the types really are different from one another
// rather than differing only in name.
struct RockRecipe {
  int cuts = 0;           // fracture planes
  float cut_flat = 0.f;   // 0 a dent, 1 a dead flat face
  float round = 0.f;      // abrasion, 0..1
  float bump = 0.f;       // weathering amplitude, share of the radius
  float bump_freq = 3.f;  // its base frequency
  float pit = 0.f;        // pits and vesicles, share of the radius
  int pit_count = 0;
  float flat = 1.f;       // axis scales: y, then x
  float elong = 1.f;
  // >0: parted along bedding - two parallel flat faces this far either side
  // of the middle, as a share of the radius. A bedded rock is NOT a squashed
  // rock: it has two flat faces and broken angular edges, and squashing gives
  // a smooth lens instead.
  float bed = 0.f;
  int sides = 0;          // >0: a prism of this many sides (columnar)
  bool flat_bottom = false; // sits on the ground rather than in it
  int detail = 3;
};
RockRecipe rock_recipe(RockType t);

} // namespace gpx
