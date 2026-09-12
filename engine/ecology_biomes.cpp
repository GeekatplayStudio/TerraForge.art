// Geekatplay TerraForge - the biomes: named sets of groups with their shares
// (gpx/ecology.hpp).
//
// A biome says what is there and in what proportion. It does NOT say how the
// things relate to each other - that is in the rules, which are about groups
// and are therefore true in every biome that uses those groups. A pine wood
// on Earth and a plantation on a terraformed Mars both have conifers keeping
// a bare circle under themselves, because that is a fact about conifers, not
// about Earth.
//
// This is what makes the table worth extending. Adding a biome is naming
// groups and shares - a dozen lines - and everything about how they sit
// together comes free. Adding a new MODEL is assigning it a group, and it is
// then placed correctly in every biome that group appears in, including the
// ones written before the model existed.
#include "gpx/ecology.hpp"

namespace gpx {
namespace eco {

namespace {

BiomeMember m(const char *group, float share, float density_ha = 0.f, float scale = 1.f) {
  BiomeMember x;
  x.group = group;
  x.share = share;
  x.density_ha = density_ha;
  x.scale = scale;
  return x;
}

Biome b(const char *id, const char *name, const char *family, float moisture, float rockiness,
        const char *note, std::vector<BiomeMember> members) {
  Biome x;
  x.id = id;
  x.name = name;
  x.family = family;
  x.moisture = moisture;
  x.rockiness = rockiness;
  x.note = note;
  x.members = std::move(members);
  return x;
}

} // namespace

const std::vector<Biome> &biomes() {
  static const std::vector<Biome> v = {
      // ------------------------------------------------------------ forest
      b("pine_wood", "Pine wood", "forest", 0.45f, 0.35f,
        "Closed conifer stand: bare needle floor, moss on the stones, little else.",
        {m("boulder", 1.f, 14.f), m("cobble", 1.f, 90.f), m("deadwood", 1.f, 12.f),
         m("conifer", 1.f, 85.f), m("snag", 0.15f, 6.f), m("sapling", 0.4f, 40.f),
         m("fern", 0.5f, 120.f), m("moss", 1.f, 900.f), m("lichen", 0.5f, 400.f),
         m("litter", 1.f, 1400.f), m("mushroom", 0.5f, 90.f)}),
      b("oak_wood", "Oak wood", "forest", 0.6f, 0.25f,
        "Broadleaf: light reaches the floor in spring, so the ground is green.",
        {m("boulder", 1.f, 9.f), m("deadwood", 1.f, 18.f), m("canopy_tree", 1.f, 55.f),
         m("understory_tree", 0.5f, 30.f), m("snag", 0.2f, 5.f), m("bramble", 0.6f, 60.f),
         m("fern", 0.8f, 200.f), m("wood_herb", 1.f, 900.f), m("grass", 0.3f, 300.f),
         m("moss", 0.8f, 500.f), m("litter", 1.f, 1600.f), m("mushroom", 0.6f, 120.f)}),
      b("birch_wood", "Birch wood", "forest", 0.55f, 0.3f,
        "Open and light: grass and heath run right through it.",
        {m("boulder", 0.8f, 12.f), m("canopy_tree", 1.f, 70.f), m("sapling", 0.5f, 60.f),
         m("heath", 0.8f, 400.f), m("grass", 1.f, 900.f), m("fern", 0.4f, 120.f),
         m("moss", 0.6f, 400.f), m("litter", 0.8f, 900.f)}),
      b("rainforest", "Rainforest", "forest", 0.95f, 0.15f,
        "Layered and wet: everything is competing for the same light.",
        {m("deadwood", 1.f, 30.f), m("canopy_tree", 1.f, 110.f), m("palm", 0.6f, 50.f),
         m("understory_tree", 1.f, 90.f), m("fern", 1.f, 500.f), m("shrub", 0.8f, 200.f),
         m("wood_herb", 0.8f, 700.f), m("moss", 1.f, 1200.f), m("litter", 1.f, 1800.f),
         m("mushroom", 0.7f, 200.f)}),
      b("taiga", "Taiga", "forest", 0.5f, 0.5f,
        "Thin cold conifer forest over rock and lichen.",
        {m("outcrop", 1.f, 4.f), m("boulder", 1.f, 26.f), m("cobble", 1.f, 140.f),
         m("conifer", 1.f, 45.f), m("snag", 0.3f, 8.f), m("heath", 0.8f, 350.f),
         m("lichen", 1.f, 900.f), m("moss", 0.9f, 700.f), m("deadwood", 0.6f, 10.f)}),
      b("wood_edge", "Wood edge", "forest", 0.6f, 0.2f,
        "Where a wood meets a field: the densest, scrubbiest ground there is.",
        {m("canopy_tree", 0.5f, 20.f), m("understory_tree", 1.f, 60.f), m("bramble", 1.f, 250.f),
         m("shrub", 1.f, 180.f), m("grass", 1.f, 1200.f), m("flower", 0.8f, 500.f),
         m("litter", 0.5f, 400.f)}),
      b("old_growth", "Old growth", "forest", 0.7f, 0.3f,
        "Few very large trees, much dead wood, deep shade.",
        {m("boulder", 1.f, 10.f), m("deadwood", 1.f, 45.f), m("canopy_tree", 1.f, 22.f),
         m("snag", 0.6f, 12.f), m("understory_tree", 0.7f, 40.f), m("fern", 1.f, 320.f),
         m("wood_herb", 1.f, 800.f), m("moss", 1.f, 1400.f), m("mushroom", 1.f, 260.f),
         m("litter", 1.f, 1500.f)}),

      // ------------------------------------------------------------- field
      b("meadow", "Meadow", "field", 0.6f, 0.1f, "Unimproved grassland in flower.",
        {m("cobble", 0.3f, 20.f), m("grass", 1.f, 4000.f), m("flower", 1.f, 1600.f),
         m("tussock", 0.4f, 300.f), m("shrub", 0.1f, 6.f)}),
      b("pasture", "Pasture", "field", 0.6f, 0.15f, "Grazed: short, even, a few thistles.",
        {m("grass", 1.f, 5000.f), m("weed", 0.3f, 200.f), m("cobble", 0.2f, 25.f),
         m("canopy_tree", 0.05f, 2.f)}),
      b("moorland", "Moorland", "field", 0.65f, 0.4f, "Heather and peat over stone.",
        {m("outcrop", 1.f, 3.f), m("boulder", 1.f, 20.f), m("heath", 1.f, 2600.f),
         m("tussock", 1.f, 900.f), m("lichen", 0.8f, 600.f), m("moss", 0.6f, 500.f)}),
      b("steppe", "Steppe", "field", 0.3f, 0.2f, "Dry open grassland, wind-combed.",
        {m("cobble", 0.5f, 60.f), m("tussock", 1.f, 1800.f), m("grass", 0.7f, 1400.f),
         m("desert_scrub", 0.4f, 120.f), m("boulder", 0.3f, 5.f)}),
      b("alpine_meadow", "Alpine meadow", "field", 0.6f, 0.6f,
        "Short turf between stones, flowering hard and briefly.",
        {m("outcrop", 1.f, 6.f), m("boulder", 1.f, 40.f), m("cobble", 1.f, 260.f),
         m("grass", 1.f, 2600.f), m("flower", 1.f, 2000.f), m("lichen", 1.f, 1200.f),
         m("moss", 0.6f, 500.f)}),
      b("farm_field", "Farm field", "farm", 0.6f, 0.05f, "A worked crop, with weeds at the margin.",
        {m("crop", 1.f, 9000.f), m("weed", 0.15f, 120.f), m("hedge", 0.1f, 30.f)}),
      b("orchard", "Orchard", "farm", 0.6f, 0.1f, "Planted rows, mown between.",
        {m("understory_tree", 1.f, 110.f), m("grass", 1.f, 3000.f), m("flower", 0.3f, 300.f)}),

      // ------------------------------------------------------------ desert
      b("sand_desert", "Sand desert", "desert", 0.04f, 0.1f, "Dunes, and almost nothing on them.",
        {m("dune", 1.f, 1.f), m("desert_scrub", 1.f, 40.f), m("cobble", 0.3f, 30.f)}),
      b("rock_desert", "Rock desert", "desert", 0.06f, 0.8f,
        "Hamada: broken stone pavement, scrub in the cracks.",
        {m("outcrop", 1.f, 8.f), m("boulder", 1.f, 60.f), m("cobble", 1.f, 900.f),
         m("desert_scrub", 1.f, 90.f), m("lichen", 0.6f, 300.f)}),
      b("sonoran", "Cactus desert", "desert", 0.1f, 0.4f, "Saguaro and creosote, widely spaced.",
        {m("boulder", 1.f, 30.f), m("cobble", 1.f, 300.f), m("columnar_cactus", 1.f, 25.f),
         m("succulent", 1.f, 160.f), m("desert_scrub", 1.f, 300.f), m("debris", 0.4f, 200.f)}),
      b("badlands", "Badlands", "desert", 0.12f, 0.9f, "Eroded rock, little life, much debris.",
        {m("outcrop", 1.f, 14.f), m("boulder", 1.f, 90.f), m("cobble", 1.f, 1200.f),
         m("debris", 1.f, 600.f), m("desert_scrub", 0.4f, 40.f)}),
      b("salt_flat", "Salt flat", "desert", 0.2f, 0.2f, "Bare pan with a rim of scrub.",
        {m("cobble", 0.4f, 60.f), m("desert_scrub", 0.3f, 20.f), m("debris", 0.3f, 80.f)}),

      // -------------------------------------------------------------- water
      b("marsh", "Marsh", "water", 0.95f, 0.05f, "Reed bed and standing water.",
        {m("reed", 1.f, 3000.f), m("tussock", 0.6f, 400.f), m("deadwood", 0.3f, 15.f),
         m("moss", 0.4f, 300.f)}),
      b("riverbank", "Riverbank", "water", 0.85f, 0.35f, "Gravel, willow and reed at the edge.",
        {m("cobble", 1.f, 1400.f), m("boulder", 1.f, 40.f), m("canopy_tree", 0.5f, 25.f),
         m("reed", 0.8f, 600.f), m("grass", 1.f, 1800.f), m("deadwood", 0.5f, 20.f)}),
      b("shore", "Shore", "water", 0.6f, 0.5f, "Wave-sorted stone and drift.",
        {m("boulder", 1.f, 50.f), m("cobble", 1.f, 3000.f), m("deadwood", 0.5f, 25.f),
         m("debris", 0.6f, 200.f), m("lichen", 0.5f, 300.f)}),
      b("reef", "Coral reef", "underwater", 1.f, 0.7f, "Heads, fans and the sand between.",
        {m("coral_head", 1.f, 400.f), m("boulder", 0.4f, 40.f), m("sea_fan", 1.f, 300.f),
         m("seagrass", 1.f, 2000.f)}),
      b("kelp_forest", "Kelp forest", "underwater", 1.f, 0.6f, "Holdfasts on rock, canopy above.",
        {m("outcrop", 1.f, 10.f), m("boulder", 1.f, 120.f), m("kelp", 1.f, 700.f),
         m("sea_fan", 0.4f, 120.f), m("seagrass", 0.5f, 600.f)}),
      b("seagrass_bed", "Seagrass bed", "underwater", 1.f, 0.1f, "Sand, and a meadow on it.",
        {m("seagrass", 1.f, 6000.f), m("cobble", 0.2f, 60.f), m("coral_head", 0.1f, 20.f)}),

      // -------------------------------------------------------------- alien
      b("mars_plain", "Martian plain", "alien", 0.f, 0.75f,
        "Regolith and scattered ejecta. Nothing grows: the rules still place "
        "the stone correctly, and stone is most of what a world is.",
        {m("boulder", 1.f, 90.f), m("cobble", 1.f, 2200.f), m("debris", 1.f, 700.f),
         m("outcrop", 0.4f, 5.f)}),
      b("mars_canyon", "Martian canyon", "alien", 0.f, 0.95f, "Cliff, talus and dust.",
        {m("outcrop", 1.f, 22.f), m("boulder", 1.f, 260.f), m("cobble", 1.f, 3400.f),
         m("debris", 1.f, 1200.f)}),
      b("lunar", "Lunar regolith", "alien", 0.f, 0.6f, "Fine dust, ejecta, no weathering.",
        {m("boulder", 1.f, 60.f), m("cobble", 1.f, 2600.f), m("debris", 1.f, 900.f)}),
      b("volcanic", "Volcanic field", "alien", 0.1f, 0.95f, "Fresh rock, the first lichen on it.",
        {m("outcrop", 1.f, 18.f), m("boulder", 1.f, 200.f), m("cobble", 1.f, 2000.f),
         m("lichen", 0.6f, 400.f), m("moss", 0.2f, 150.f)}),
      b("alien_fungal", "Fungal world", "alien", 0.9f, 0.3f,
        "An invented ecology that still obeys the rules: the tall things keep "
        "their ground clear, the low things gather on the stone.",
        {m("boulder", 1.f, 40.f), m("deadwood", 0.6f, 20.f), m("understory_tree", 1.f, 90.f),
         m("mushroom", 1.f, 2000.f), m("moss", 1.f, 1600.f), m("fern", 0.6f, 300.f)}),
      b("alien_crystal", "Crystal waste", "alien", 0.f, 1.f, "Spires from a floor of shards.",
        {m("outcrop", 1.f, 26.f), m("columnar_cactus", 1.f, 60.f), m("cobble", 1.f, 3000.f),
         m("debris", 1.f, 800.f)}),

      // -------------------------------------------------------------- built
      b("street_verge", "Street verge", "built", 0.5f, 0.05f,
        "Planted trees, mown grass, and what blows into the hedge.",
        {m("street_tree", 1.f, 40.f), m("hedge", 0.6f, 80.f), m("grass", 1.f, 3000.f),
         m("weed", 0.5f, 400.f), m("litter_built", 0.4f, 200.f)}),
      b("park", "Park", "built", 0.55f, 0.1f, "Specimen trees over mown turf.",
        {m("street_tree", 0.6f, 18.f), m("canopy_tree", 1.f, 20.f), m("grass", 1.f, 4000.f),
         m("flower", 0.4f, 400.f), m("planter", 0.2f, 8.f)}),
      b("wasteland", "Wasteland", "built", 0.45f, 0.4f,
        "Abandoned ground: rubble, buddleia, and everything opportunistic.",
        {m("debris", 1.f, 900.f), m("cobble", 1.f, 700.f), m("shrub", 1.f, 250.f),
         m("weed", 1.f, 2600.f), m("grass", 0.8f, 1600.f), m("litter_built", 0.6f, 300.f),
         m("sapling", 0.4f, 60.f)}),
      b("courtyard", "Courtyard", "built", 0.5f, 0.05f, "Planters and a little shade.",
        {m("planter", 1.f, 40.f), m("street_tree", 0.4f, 12.f), m("hedge", 0.6f, 60.f),
         m("weed", 0.3f, 150.f)}),
  };
  return v;
}

const Biome *biome_of(const std::string &id) {
  for (const Biome &x : biomes())
    if (x.id == id) return &x;
  return nullptr;
}

std::vector<const Biome *> biomes_in(const std::string &family) {
  std::vector<const Biome *> out;
  for (const Biome &x : biomes())
    if (family.empty() || x.family == family) out.push_back(&x);
  return out;
}

} // namespace eco
} // namespace gpx
