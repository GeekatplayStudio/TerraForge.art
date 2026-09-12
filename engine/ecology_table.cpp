// Geekatplay TerraForge - the groups things belong to, and the rules between
// them (gpx/ecology.hpp).
//
// The groups are written from how ground actually assembles, not from what a
// model library happens to contain. A boulder is a boulder whether it is
// granite in Scotland or basalt on Mars; what changes between those is which
// biome asks for it and how much, not what a boulder is. That is what makes
// the rules worth writing down: they are about the relationship, and the
// relationship is the part that stays true.
//
// Every rule here is an observation anyone can check by walking outside. They
// are commented with the observation rather than the number, because the
// number is only ever an attempt at the observation and someone will want to
// improve it.
#include "gpx/ecology.hpp"
#include <algorithm>
#include <cctype>

namespace gpx {
namespace eco {

namespace {

Group g(const char *name, const char *plural, Substrate sub, Stance st, Light li, float foot,
        float height, int tier, float slope, float moisture, float mwidth, float var, float lean) {
  Group x;
  x.name = name;
  x.plural = plural;
  x.substrate = sub;
  x.stance = st;
  x.light = li;
  x.footprint_m = foot;
  x.height_m = height;
  x.tier = tier;
  x.slope_max_deg = slope;
  x.moisture = moisture;
  x.moisture_width = mwidth;
  x.size_variation = var;
  x.lean = lean;
  return x;
}

Relation r(const char *of, const char *near, float aff, float aff_m, float rep, float rep_m,
           const char *why) {
  Relation x;
  x.of = of;
  x.near = near;
  x.affinity = aff;
  x.affinity_m = aff_m;
  x.repulsion = rep;
  x.repulsion_m = rep_m;
  x.why = why;
  return x;
}

} // namespace

// ----------------------------------------------------------------- groups
//
// Tier is the order the ground assembles, and it is the whole reason this
// works: what lies on bare earth is placed first, and everything after it can
// be placed in relation to what is already there. Getting the tier wrong is
// the one mistake that cannot be repaired by tuning - moss placed before the
// boulders has nothing to grow on.
const std::vector<Group> &groups() {
  static const std::vector<Group> v = {
      // ---- tier 0: what the ground itself puts there ----------------------
      g("outcrop", "outcrops", Substrate::Ground, Stance::Surface, Light::Any, 6.f, 3.f, 0, 70.f,
        0.3f, 0.6f, 0.5f, 1.f),
      g("boulder", "boulders", Substrate::Ground, Stance::Surface, Light::Any, 1.6f, 1.4f, 0, 45.f,
        0.4f, 0.6f, 0.7f, 1.f),
      g("cobble", "cobbles", Substrate::Ground, Stance::Surface, Light::Any, 0.3f, 0.2f, 0, 35.f,
        0.4f, 0.6f, 0.6f, 1.f),
      // Tier 2, not 0: fallen wood lies where the trees are, so it has to be
      // placed after them or the rule that puts it there has nothing to be
      // about. It is on the ground, but it is not something the ground put
      // there - that is the distinction tier draws.
      g("deadwood", "fallen wood", Substrate::Ground, Stance::Surface, Light::Any, 1.2f, 0.4f, 2,
        30.f, 0.6f, 0.4f, 0.5f, 1.f),
      g("dune", "dunes", Substrate::Ground, Stance::Surface, Light::Full, 12.f, 2.f, 0, 30.f, 0.05f,
        0.15f, 0.5f, 1.f),
      g("coral_head", "coral heads", Substrate::Ground, Stance::Surface, Light::Any, 1.2f, 0.9f, 0,
        40.f, 1.f, 0.1f, 0.6f, 0.7f),

      // ---- tier 1: what stands up out of it -------------------------------
      g("canopy_tree", "canopy trees", Substrate::Ground, Stance::Upright, Light::Full, 4.5f, 22.f,
        1, 32.f, 0.55f, 0.35f, 0.3f, 0.f),
      g("conifer", "conifers", Substrate::Ground, Stance::Upright, Light::Full, 3.f, 24.f, 1, 38.f,
        0.45f, 0.4f, 0.3f, 0.f),
      g("understory_tree", "understory trees", Substrate::Ground, Stance::Upright, Light::Partial,
        2.2f, 7.f, 1, 35.f, 0.55f, 0.35f, 0.35f, 0.f),
      g("palm", "palms", Substrate::Ground, Stance::Upright, Light::Full, 2.5f, 14.f, 1, 25.f, 0.5f,
        0.4f, 0.25f, 0.f),
      g("snag", "standing dead trees", Substrate::Ground, Stance::Upright, Light::Any, 1.5f, 9.f, 1,
        30.f, 0.5f, 0.5f, 0.4f, 0.f),
      g("columnar_cactus", "columnar cacti", Substrate::Ground, Stance::Upright, Light::Full, 1.2f,
        5.f, 1, 30.f, 0.08f, 0.12f, 0.35f, 0.f),
      // Rock, not ground: a holdfast is a grip, not a root, and kelp simply
      // is not found on sand.
      g("kelp", "kelp", Substrate::Rock, Stance::Upright, Light::Partial, 0.8f, 6.f, 1, 35.f, 1.f,
        0.1f, 0.4f, 0.f),

      // ---- tier 2: what lives among them ----------------------------------
      g("shrub", "shrubs", Substrate::Ground, Stance::Upright, Light::Partial, 1.4f, 1.6f, 2, 40.f,
        0.5f, 0.4f, 0.4f, 0.f),
      g("bramble", "bramble", Substrate::Ground, Stance::Upright, Light::Partial, 1.8f, 1.2f, 2,
        35.f, 0.6f, 0.35f, 0.45f, 0.f),
      g("sapling", "saplings", Substrate::Ground, Stance::Upright, Light::Partial, 0.6f, 2.f, 2,
        35.f, 0.55f, 0.35f, 0.4f, 0.f),
      g("fern", "ferns", Substrate::Ground, Stance::Upright, Light::Shade, 0.7f, 0.8f, 2, 35.f,
        0.7f, 0.3f, 0.35f, 0.f),
      g("reed", "reeds", Substrate::Water, Stance::Upright, Light::Full, 0.4f, 1.8f, 2, 12.f, 0.95f,
        0.12f, 0.3f, 0.f),
      g("succulent", "succulents", Substrate::Ground, Stance::Upright, Light::Full, 0.5f, 0.4f, 2,
        35.f, 0.1f, 0.15f, 0.4f, 0.f),
      g("sea_fan", "sea fans", Substrate::Rock, Stance::Upright, Light::Partial, 0.5f, 0.7f, 2,
        60.f, 1.f, 0.1f, 0.4f, 0.2f),

      // ---- tier 3: what covers the ground ---------------------------------
      g("grass", "grass", Substrate::Ground, Stance::Upright, Light::Full, 0.25f, 0.4f, 3, 38.f,
        0.55f, 0.4f, 0.3f, 0.1f),
      g("tussock", "tussocks", Substrate::Ground, Stance::Upright, Light::Full, 0.5f, 0.7f, 3, 40.f,
        0.6f, 0.4f, 0.35f, 0.1f),
      g("heath", "heather", Substrate::Ground, Stance::Upright, Light::Full, 0.4f, 0.4f, 3, 40.f,
        0.4f, 0.3f, 0.3f, 0.1f),
      g("flower", "flowers", Substrate::Ground, Stance::Upright, Light::Full, 0.2f, 0.4f, 3, 35.f,
        0.55f, 0.4f, 0.3f, 0.f),
      g("wood_herb", "woodland herbs", Substrate::Ground, Stance::Upright, Light::Shade, 0.2f, 0.2f,
        3, 30.f, 0.7f, 0.3f, 0.3f, 0.f),
      g("desert_scrub", "desert scrub", Substrate::Ground, Stance::Upright, Light::Full, 0.6f, 0.5f,
        3, 30.f, 0.12f, 0.18f, 0.45f, 0.f),
      g("crop", "crop", Substrate::Ground, Stance::Upright, Light::Full, 0.3f, 0.9f, 3, 10.f, 0.6f,
        0.3f, 0.12f, 0.f),
      g("seagrass", "seagrass", Substrate::Ground, Stance::Upright, Light::Full, 0.2f, 0.5f, 3,
        20.f, 1.f, 0.1f, 0.3f, 0.f),

      // ---- tier 4: what lies over everything ------------------------------
      g("moss", "moss", Substrate::Rock, Stance::Surface, Light::Shade, 0.35f, 0.05f, 4, 90.f, 0.8f,
        0.25f, 0.5f, 1.f),
      g("lichen", "lichen", Substrate::Rock, Stance::Surface, Light::Any, 0.25f, 0.02f, 4, 90.f,
        0.3f, 0.5f, 0.5f, 1.f),
      g("litter", "leaf litter", Substrate::Ground, Stance::Surface, Light::Any, 0.4f, 0.03f, 4,
        30.f, 0.6f, 0.4f, 0.5f, 1.f),
      g("mushroom", "fungi", Substrate::Ground, Stance::Upright, Light::Shade, 0.12f, 0.1f, 4, 25.f,
        0.8f, 0.2f, 0.4f, 0.f),
      g("debris", "debris", Substrate::Ground, Stance::Surface, Light::Any, 0.3f, 0.1f, 4, 30.f,
        0.5f, 0.5f, 0.6f, 1.f),
      g("litter_built", "street litter", Substrate::Ground, Stance::Surface, Light::Any, 0.2f,
        0.05f, 4, 15.f, 0.5f, 0.5f, 0.5f, 1.f),

      // ---- the built world -------------------------------------------------
      g("street_tree", "street trees", Substrate::Ground, Stance::Upright, Light::Full, 2.5f, 9.f,
        1, 8.f, 0.5f, 0.5f, 0.15f, 0.f),
      g("hedge", "hedge", Substrate::Ground, Stance::Upright, Light::Partial, 0.8f, 1.6f, 2, 20.f,
        0.55f, 0.4f, 0.1f, 0.f),
      g("weed", "weeds", Substrate::Ground, Stance::Upright, Light::Any, 0.2f, 0.3f, 3, 30.f, 0.5f,
        0.5f, 0.45f, 0.f),
      g("planter", "planters", Substrate::Ground, Stance::Surface, Light::Any, 0.8f, 0.8f, 1, 6.f,
        0.5f, 0.5f, 0.1f, 0.f),
  };
  return v;
}

const Group *group_of(const std::string &name) {
  for (const Group &x : groups())
    if (x.name == name) return &x;
  return nullptr;
}

// -------------------------------------------------------------- the rules
//
// Each is an observation. They are stated in the order they were noticed on
// the ground: what gathers on what, what clears a space around itself, what
// only ever appears in the lee of something else.
const std::vector<Relation> &relations() {
  static const std::vector<Relation> v = {
      // ---- stone ----------------------------------------------------------
      r("moss", "boulder", 1.f, 2.0f, -1.f, 1.3,
        "Moss is on the stone and nowhere else. Negative repulsion is the "
        "rule that says ONLY within that radius - it is what makes a group "
        "sit on another rather than merely near it."),
      r("moss", "outcrop", 1.f, 5.0f, -1.f, 4.0, "The same, at the scale of an outcrop."),
      r("lichen", "boulder", 1.f, 2.0f, -1.f, 1.2,
        "Lichen takes the dry exposed faces stone gives it; it is the first "
        "thing on bare rock and often the only thing."),
      r("lichen", "outcrop", 1.f, 5.0f, -1.f, 4.0, "As on a boulder."),
      r("cobble", "boulder", 0.75f, 5.f, 0.f, 2.0f,
        "Stone breaks up where stone already is: a boulder sits in its own "
        "scatter of the pieces off it."),
      r("moss", "deadwood", 0.9f, 1.5f, -1.f, 1.0,
        "A fallen trunk is as good a substrate as a stone, and wetter."),
      r("mushroom", "deadwood", 0.95f, 2.5f, 0.f, 1.0f,
        "Fungi are where the dead wood is; that is what they are eating."),

      // ---- under a canopy --------------------------------------------------
      r("grass", "conifer", 0.f, 6.4f, 1.f, 3.2,
        "No grass under the pines. Needle litter is acid and the shade is "
        "year-round, and the bare circle under a spruce is one of the most "
        "recognisable things in a wood."),
      r("grass", "canopy_tree", 0.f, 5.2f, 0.75f, 2.6,
        "A broadleaf lets light through in spring and its litter rots, so "
        "the bare ring is smaller and less complete than a conifer's."),
      r("flower", "conifer", 0.f, 6.0f, 1.f, 3.0, "As for grass: nothing flowers in that shade."),
      r("heath", "conifer", 0.f, 6.0f, 0.9f, 3.0, "Heather wants the open ground between."),
      r("litter", "conifer", 1.f, 4.0f, 0.f, 1.6f,
        "The needles are exactly where the tree is - the litter layer is the "
        "other half of why nothing grows there."),
      r("litter", "canopy_tree", 1.f, 5.0f, 0.f, 2.0f, "A broadleaf drops its leaves round itself."),
      r("wood_herb", "canopy_tree", 0.8f, 6.0f, 0.4f, 1.6,
        "Wood sorrel and bluebell want the shade a broadleaf gives, but not "
        "the dry root plate right at the trunk."),
      r("fern", "canopy_tree", 0.7f, 8.0f, 0.5f, 2.0,
        "Ferns fill the damp shade under a wood and stop short of the trunk."),
      r("fern", "conifer", 0.5f, 7.0f, 0.7f, 2.6, "Less under conifers, and further out."),
      r("bramble", "canopy_tree", 0.6f, 10.f, 0.8f, 3.5,
        "Bramble is a wood-edge plant: it wants the light at the gap, not "
        "the dark under the crown."),
      r("sapling", "canopy_tree", -0.4f, 9.f, 0.9f, 3.0,
        "Nothing replaces a tree while it is standing; saplings are in the "
        "gaps where one fell."),
      r("mushroom", "canopy_tree", 0.7f, 6.0f, 0.f, 2.4f,
        "Fungi follow the roots out from the trunk."),

      // ---- among the trees -------------------------------------------------
      r("shrub", "canopy_tree", 0.35f, 12.f, 0.6f, 3.0,
        "The understory is between the trees, not beneath them."),
      r("understory_tree", "canopy_tree", -0.3f, 14.f, 0.85f, 5.0,
        "A holly or a hazel waits in the gap between the big trees."),
      r("snag", "canopy_tree", 0.4f, 20.f, 0.5f, 4.0,
        "A dead tree stands where it grew, among its own kind."),
      r("deadwood", "canopy_tree", 0.8f, 9.0f, 0.f, 3.6f,
        "Fallen wood lies where it fell: under and around the trees."),
      r("deadwood", "conifer", 0.8f, 8.0f, 0.f, 3.2f, "As under broadleaves."),

      // ---- stone and the things that avoid it ------------------------------
      r("grass", "boulder", 0.f, 2.2f, 1.f, 1.1,
        "Grass does not grow through a stone. The hole is the stone's own "
        "footprint and no more - grass comes right up to it."),
      r("grass", "outcrop", 0.f, 7.0f, 1.f, 3.5, "As against a boulder, at its own size."),
      r("flower", "boulder", 0.25f, 2.5f, 1.f, 1.0,
        "Flowers gather in the sheltered damp at a stone's foot, and stop at "
        "the stone."),
      r("canopy_tree", "outcrop", 0.f, 10.0f, 1.f, 5.0, "A tree cannot root in bare rock."),
      r("conifer", "outcrop", 0.f, 8.0f, 1.f, 4.0,
        "A pine will take a crevice, but not the face itself."),
      r("fern", "boulder", 0.7f, 2.0f, 0.4f, 0.8,
        "Ferns love the damp shade at the north foot of a stone."),

      // ---- water's edge ----------------------------------------------------
      r("reed", "boulder", 0.f, 2.4f, 1.f, 1.2, "Reeds want mud, not stone."),
      r("seagrass", "coral_head", 0.f, 3.0f, 1.f, 1.5,
        "Seagrass is on the sand between the heads, never on them."),
      r("sea_fan", "coral_head", 1.f, 1.6f, -1.f, 1.2,
        "A sea fan is fixed to the reef structure itself."),
      r("kelp", "boulder", 0.9f, 2.5f, -1.f, 2.0,
        "Kelp holdfasts need something hard to grip; on sand there is none."),

      // ---- dry ground ------------------------------------------------------
      r("desert_scrub", "columnar_cactus", 0.2f, 6.0f, 0.8f, 2.2,
        "A saguaro keeps a ring clear: in a desert the competition is for "
        "water, and the roots reach far further than the shade."),
      r("succulent", "boulder", 0.8f, 2.0f, 0.6f, 0.9,
        "Succulents gather in the shade and run-off at a stone's foot."),
      r("columnar_cactus", "columnar_cactus", -0.5f, 8.0f, 0.9f, 4.0,
        "Spaced by their own root reach, which is what gives a cactus stand "
        "its even, almost planted look."),
      r("debris", "boulder", 0.7f, 3.0f, 0.f, 1.2f,
        "Wind-blown material piles in the lee of anything standing."),

      // ---- the built world -------------------------------------------------
      r("weed", "street_tree", 0.6f, 1.5f, -1.f, 1.2,
        "Weeds take the tree pit and the crack at its edge, which is the "
        "only unsealed ground there is."),
      r("litter_built", "hedge", 0.9f, 1.5f, 0.f, 0.6f,
        "Litter blows against the first thing that stops it."),
      r("litter_built", "planter", 0.8f, 1.2f, 0.f, 0.5f, "The same, against a planter."),
      r("grass", "street_tree", 0.f, 2.8f, 0.9f, 1.4,
        "The tree pit is bare or paved right up to the trunk."),
  };
  return v;
}

std::vector<const Relation *> relations_of(const std::string &of) {
  std::vector<const Relation *> out;
  for (const Relation &x : relations())
    if (x.of == of) out.push_back(&x);
  return out;
}

const Relation *relation_between(const std::string &of, const std::string &near) {
  for (const Relation &x : relations())
    if (x.of == of && x.near == near) return &x;
  return nullptr;
}

std::vector<BiomeMember> in_order(const Biome &b) {
  std::vector<BiomeMember> out = b.members;
  std::stable_sort(out.begin(), out.end(), [](const BiomeMember &a, const BiomeMember &c) {
    const Group *ga = group_of(a.group), *gc = group_of(c.group);
    return (ga ? ga->tier : 9) < (gc ? gc->tier : 9);
  });
  return out;
}

// ------------------------------------------------------- guessing a group
// Only ever a guess, and it says so by returning nothing when it is not sure.
// A wrong guess puts moss in mid-air; no guess asks the person instead.
std::string group_from_name(const std::string &name) {
  std::string s;
  for (char c : name) s += (char)std::tolower((unsigned char)c);
  auto has = [&s](const char *w) { return s.find(w) != std::string::npos; };
  // the specific before the general: "mossy rock" is a rock, not a moss
  if (has("boulder") || has("rock") || has("stone")) return has("small") || has("pebble") ? "cobble" : "boulder";
  if (has("outcrop") || has("cliff") || has("crag")) return "outcrop";
  if (has("pebble") || has("gravel") || has("cobble")) return "cobble";
  if (has("log") || has("stump") || has("fallen") || has("driftwood")) return "deadwood";
  if (has("snag") || has("dead tree")) return "snag";
  if (has("pine") || has("spruce") || has("fir") || has("larch") || has("cedar") || has("conifer"))
    return "conifer";
  if (has("palm")) return "palm";
  if (has("cactus") || has("saguaro")) return "columnar_cactus";
  if (has("kelp")) return "kelp";
  if (has("coral")) return "coral_head";
  if (has("seagrass") || has("eelgrass")) return "seagrass";
  if (has("reed") || has("bulrush") || has("cattail")) return "reed";
  if (has("moss")) return "moss";
  if (has("lichen")) return "lichen";
  if (has("mushroom") || has("fungus") || has("toadstool")) return "mushroom";
  if (has("fern") || has("bracken")) return "fern";
  if (has("bramble") || has("briar")) return "bramble";
  if (has("sapling") || has("seedling")) return "sapling";
  if (has("heather") || has("heath")) return "heath";
  if (has("tussock")) return "tussock";
  if (has("grass") || has("turf")) return "grass";
  if (has("flower") || has("bloom") || has("poppy") || has("daisy") || has("foxglove")) return "flower";
  if (has("succulent") || has("agave") || has("aloe")) return "succulent";
  if (has("shrub") || has("bush") || has("gorse") || has("juniper")) return "shrub";
  if (has("hedge")) return "hedge";
  if (has("weed") || has("nettle") || has("dandelion")) return "weed";
  if (has("litter") || has("leaves")) return "litter";
  if (has("debris") || has("rubble")) return "debris";
  if (has("crop") || has("wheat") || has("barley") || has("maize")) return "crop";
  if (has("tree") || has("oak") || has("beech") || has("birch") || has("maple") || has("willow"))
    return "canopy_tree";
  return std::string();
}

} // namespace eco
} // namespace gpx
