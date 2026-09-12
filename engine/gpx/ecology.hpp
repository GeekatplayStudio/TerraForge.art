#pragma once
// Geekatplay TerraForge - what grows where, and next to what (engine/ecology*.cpp).
//
// Placing a wood is not placing trees. It is placing boulders, then the moss
// that only grows on their shaded north side, then the trees in the ground
// between them, then the bare ring of needle litter each pine keeps under
// itself, then the bracken in the light gaps, then the grass in what is left.
// Every one of those is a rule about a RELATIONSHIP, and a person assembling
// a wood by hand is really re-deriving those rules object by object.
//
// So the rules are written down once, and they are written about GROUPS
// rather than about things. "Moss" is a group; a particular moss model is a
// member of it. That is the whole point: a rule that says moss gathers on
// boulders and never in the open keeps working when someone adds a new moss
// next year, because the new moss is a member of the same group. Rules by
// species would have to be rewritten every time the library grew.
//
// A biome is a named set of groups with their shares - a pine wood, a chalk
// down, a Martian plain, a reef - and building one lays down a stack of
// population layers in the order the ground actually assembles: what sits on
// the bare earth first, what depends on that second, ground cover last. The
// layers already know how to gather around or keep away from the layer below
// them (EcosystemLayer's affinity and repulsion), so the ecology is expressed
// in the terms the populations already speak.
#include <string>
#include <vector>

namespace gpx {
namespace eco {

// What a thing needs under it. A group that wants rock cannot be scattered on
// bare soil, and the placement puts it where its substrate is instead.
enum class Substrate {
  Ground,   // soil, sand, regolith - the terrain itself
  Rock,     // on or against stone: moss, lichen, saxifrage
  Bark,     // on a trunk: ivy, epiphytes, bracket fungus
  Water,    // in it or on it: reeds, lilies, kelp
  Any,
};

// How a thing stands.
enum class Stance {
  Upright,  // grows against gravity whatever the ground does: a tree
  Surface,  // lies along the ground it sits on: a boulder, a fallen log
  Hanging,  // hangs from what it is on: moss on a branch, a vine
};

// How much light it needs. This is what keeps grass out from under the pines
// and puts bracken in the gaps.
enum class Light {
  Full,     // open ground only: heather, most grasses, cactus
  Partial,  // a gap or a wood edge: bracken, bramble, hazel
  Shade,    // under a closed canopy: moss, wood sorrel, fern
  Any,
};

// One kind of thing, and what it needs. The footprint is what it actually
// occupies on the ground, which decides how close two of them may be.
struct Group {
  std::string name;         // "boulder", "canopy tree", "moss", ...
  std::string plural;       // what to call a population of them
  Substrate substrate = Substrate::Ground;
  Stance stance = Stance::Upright;
  Light light = Light::Any;
  float footprint_m = 1.f;  // its own radius on the ground
  float height_m = 1.f;     // typical, so a preset can size a scene
  // Where it sits in the order things are laid down: 0 is the bare ground
  // (boulders, outcrops), 1 what stands on it (trees), 2 what lives under
  // and among those (shrubs), 3 ground cover, 4 the litter over everything.
  int tier = 3;
  float slope_max_deg = 40.f;
  // 0 dry .. 1 waterlogged: where along that it prefers to be, and how
  // fussy it is about it.
  float moisture = 0.5f, moisture_width = 0.5f;
  float size_variation = 0.35f;
  // How far it tips with the ground: 0 stands up, 1 lies along it.
  float lean = 0.f;
};

// One rule about two groups: how `of` behaves near `near`.
//
// Both numbers matter and they say different things. Affinity is a gradient -
// more of them the closer you get - and repulsion is a hole: nothing at all
// inside the radius. Real ecology needs both at once, and the interesting
// cases are exactly the ones that use both: bracken gathers near trees
// (affinity) but not in the dark directly under them (repulsion).
struct Relation {
  std::string of;        // the group being placed
  std::string near;      // the group already on the ground
  float affinity = 0.f;  // + gathers around it, - avoids its neighbourhood
  float affinity_m = 8.f;
  float repulsion = 0.f; // + a clear ring around it, - ONLY inside that ring
  float repulsion_m = 3.f;
  const char *why = "";  // the observation it comes from, for the tooltip
};

// A biome: which groups are in it, in what proportion, and how thickly.
struct BiomeMember {
  std::string group;
  float share = 1.f;        // against the other members of its tier
  float density_ha = 0.f;   // 0 takes the group's own usual density
  float scale = 1.f;
};
struct Biome {
  std::string id;           // "temperate_pine_wood"
  std::string name;         // "Pine wood"
  std::string family;       // "forest", "desert", "field", "water", "alien", "built"
  std::string note;         // one line: where on earth, or what it stands for
  std::vector<BiomeMember> members;
  float moisture = 0.5f;    // the ground it assumes
  float rockiness = 0.3f;
};

// The tables. Every group the rules know, every rule between them, and every
// biome built out of them (engine/ecology_table.cpp, ecology_biomes.cpp).
const std::vector<Group> &groups();
const Group *group_of(const std::string &name);
const std::vector<Relation> &relations();
// Every rule about how `of` behaves near anything already placed.
std::vector<const Relation *> relations_of(const std::string &of);
// The one rule for this pair, or null.
const Relation *relation_between(const std::string &of, const std::string &near);
const std::vector<Biome> &biomes();
const Biome *biome_of(const std::string &id);
// Every biome of a family ("forest"), for a menu.
std::vector<const Biome *> biomes_in(const std::string &family);

// A biome laid out in the order the ground assembles: its members sorted by
// tier, so what sits on bare earth comes first and the rules about it are
// already true when the next tier is placed.
std::vector<BiomeMember> in_order(const Biome &b);

// Which group a thing belongs to, guessed from its name when nobody has
// said. "mossy_rock_01" is a boulder, "fern_02" is a fern. Empty when it
// cannot tell, and the caller should ask rather than guess wrong.
std::string group_from_name(const std::string &name);

} // namespace eco
} // namespace gpx
