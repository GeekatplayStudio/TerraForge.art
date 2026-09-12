// Geekatplay TerraForge - the ecology: groups, the rules between them, and
// the biomes built out of them (engine/gpx/ecology.hpp).
//
// The tables are meant to grow - that is the whole point of writing the rules
// about groups rather than about models - so what is asserted here is that
// they stay COHERENT as they do. A rule naming a group that does not exist, a
// biome asking for something nobody defined, moss placed before the stone it
// grows on: each of those is silent at runtime and shows up only as scenery
// that looks assembled.
//
// The promises:
//   1. every rule and every biome names groups that exist;
//   2. a thing is always placed after what it depends on - the tier order;
//   3. the rules say what they mean to say: no grass under the conifers,
//      moss only on stone, bracken near the trees but not beneath them;
//   4. a biome lays out in an order that lets its rules be expressed;
//   5. a name is guessed into the right group, or into none at all.
#include "gpx/ecology.hpp"
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

using namespace gpx::eco;

static int fails = 0;
static void check(bool ok, const std::string &what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what.c_str());
    ++fails;
  }
}

static void test_tables_are_whole() {
  std::printf("the tables hang together...\n");
  check(groups().size() >= 25, "there are groups for the things a scene is made of");
  check(biomes().size() >= 25, "and biomes across the families");
  std::map<std::string, int> seen;
  for (const Group &g : groups()) {
    check(!g.name.empty() && !g.plural.empty(), "every group is named");
    check(g.footprint_m > 0.f, g.name + " occupies some ground");
    check(g.tier >= 0 && g.tier <= 4, g.name + "'s tier is one of the five");
    check(++seen[g.name] == 1, g.name + " is defined once");
  }
  for (const Relation &r : relations()) {
    check(group_of(r.of) != nullptr, "a rule is about a group that exists: '" + r.of + "'");
    check(group_of(r.near) != nullptr, "and against one that exists: '" + r.near + "'");
    check(r.why && *r.why, r.of + "/" + r.near + ": the rule says what observation it came from");
    // a radius that means nothing today is read as a number tomorrow
    check(r.affinity_m > 0.f && r.repulsion_m > 0.f, r.of + "/" + r.near + " has real radii");
    check(std::abs(r.affinity) > 1e-6f || std::abs(r.repulsion) > 1e-6f,
          r.of + "/" + r.near + " actually says something");
  }
  for (const Biome &b : biomes()) {
    check(!b.members.empty(), b.id + " has something in it");
    check(!b.note.empty(), b.id + " says what it is");
    for (const BiomeMember &m : b.members)
      check(group_of(m.group) != nullptr, b.id + " asks for a group that exists: '" + m.group + "'");
  }
}

// The one mistake that cannot be repaired by tuning: a thing placed before
// what it grows on. Moss laid before the boulders has nothing to gather on.
static void test_things_come_after_what_they_need() {
  std::printf("what depends on what comes after it...\n");
  for (const Relation &r : relations()) {
    const Group *of = group_of(r.of), *on = group_of(r.near);
    if (!of || !on) continue;
    // a rule only means anything if the thing it is about is placed later
    check(of->tier >= on->tier, r.of + " (tier " + std::to_string(of->tier) + ") is placed after " +
                                    r.near + " (tier " + std::to_string(on->tier) +
                                    "), which it has a rule about");
  }
  // and a biome lays its members out in that order
  for (const Biome &b : biomes()) {
    const std::vector<BiomeMember> ord = in_order(b);
    int last = -1;
    for (const BiomeMember &m : ord) {
      const Group *g = group_of(m.group);
      if (!g) continue;
      check(g->tier >= last, b.id + " lays " + m.group + " down in order");
      last = g->tier;
    }
  }
}

// The rules a person would actually name if asked what a wood looks like.
static void test_the_rules_say_what_they_mean() {
  std::printf("the rules mean what they say...\n");

  // no grass under the pines
  const Relation *r = relation_between("grass", "conifer");
  check(r != nullptr, "there is a rule about grass under conifers");
  if (r) {
    check(r->repulsion > 0.5f, "and it clears the ground under them (" + std::to_string(r->repulsion) + ")");
    check(r->repulsion_m > 1.5f, "over a real circle (" + std::to_string(r->repulsion_m) + " m)");
    check(r->affinity <= 0.f, "grass is not drawn toward a pine");
  }
  // a broadleaf is kinder than a conifer: the bare ring is smaller
  const Relation *oak = relation_between("grass", "canopy_tree");
  if (r && oak)
    check(oak->repulsion_m < r->repulsion_m,
          "a broadleaf's bare ring is smaller than a conifer's (" + std::to_string(oak->repulsion_m) +
              " against " + std::to_string(r->repulsion_m) + ")");

  // moss is ON the stone, not merely near it: negative repulsion is the rule
  // that says "only within this radius"
  const Relation *moss = relation_between("moss", "boulder");
  check(moss != nullptr, "there is a rule about moss and boulders");
  if (moss) {
    check(moss->affinity > 0.5f, "moss gathers on the stone");
    check(moss->repulsion < -0.5f, "and ONLY there - negative repulsion, not merely nearby");
    check(moss->repulsion_m < 2.f, "within the stone's own reach (" + std::to_string(moss->repulsion_m) + " m)");
  }
  // grass stops at the stone, and comes right up to it
  const Relation *gb = relation_between("grass", "boulder");
  check(gb && gb->repulsion > 0.5f, "grass does not grow through a boulder");
  if (gb) {
    const Group *b = group_of("boulder");
    check(b && gb->repulsion_m < b->footprint_m * 1.5f,
          "and the bare patch is the stone's own footprint, not a halo (" +
              std::to_string(gb->repulsion_m) + " m)");
  }
  // near the trees but not under them: the case that needs both at once
  const Relation *br = relation_between("bramble", "canopy_tree");
  check(br != nullptr, "there is a rule about bramble and trees");
  if (br) {
    check(br->affinity > 0.f && br->repulsion > 0.f,
          "bramble is near the trees AND out of their shade - both terms at once");
    check(br->affinity_m > br->repulsion_m, "gathering further out than it is pushed back");
  }
  // A group that needs rock or bark under it must have somewhere to be. A
  // negative repulsion is the rule that says "only within this radius", and
  // without one such rule the group is scattered over open ground - which
  // for moss or lichen means in mid-air.
  //
  // Not the other way round: an "only there" rule is perfectly good for a
  // thing standing on the ground near something else, which is what weeds in
  // a tree pit are.
  for (const Group &g : groups()) {
    if (g.substrate != Substrate::Rock && g.substrate != Substrate::Bark) continue;
    bool anchored = false;
    for (const Relation *x : relations_of(g.name))
      if (x->repulsion < 0.f) {
        const Group *host = group_of(x->near);
        anchored = anchored || (host && host->tier <= g.tier);
      }
    check(anchored, g.name + " needs " +
                        (g.substrate == Substrate::Rock ? "rock" : "bark") +
                        " under it, so something must say where that is");
  }
}

static void test_biomes_read_as_places() {
  std::printf("the biomes read as places...\n");
  // the families the request named, each with something in it
  for (const char *fam : {"forest", "field", "desert", "water", "underwater", "alien", "built"})
    check(!biomes_in(fam).empty(), std::string("there are ") + fam + " biomes");

  // a pine wood has what a pine wood has, in the order it has it
  const Biome *pw = biome_of("pine_wood");
  check(pw != nullptr, "there is a pine wood");
  if (pw) {
    const std::vector<BiomeMember> ord = in_order(*pw);
    check(ord.size() >= 6, "with a real assembly of things");
    const Group *first = group_of(ord.front().group);
    check(first && first->tier == 0, "beginning with what lies on the bare ground (" +
                                         ord.front().group + ")");
    bool has_conifer = false, has_moss = false, has_grass = false;
    for (const BiomeMember &m : ord) {
      has_conifer = has_conifer || m.group == "conifer";
      has_moss = has_moss || m.group == "moss";
      has_grass = has_grass || m.group == "grass";
    }
    check(has_conifer && has_moss, "conifers and moss");
    check(!has_grass, "and no grass: under a closed pine canopy there is none");
  }
  // Mars has stone and nothing living, and the rules still place the stone
  const Biome *mars = biome_of("mars_plain");
  check(mars != nullptr, "there is a Martian plain");
  if (mars)
    for (const BiomeMember &m : mars->members) {
      const Group *g = group_of(m.group);
      check(g && g->substrate != Substrate::Water, "nothing on Mars wants water: " + m.group);
      check(g && (g->light == Light::Any || g->tier == 0),
            m.group + " does not need sunlight to be a rock");
    }
}

static void test_a_name_finds_its_group() {
  std::printf("a name finds its group...\n");
  struct Case { const char *name, *group; };
  static const Case CASES[] = {
      {"mossy_boulder_02", "boulder"},   // the noun, not the adjective
      {"granite rock large", "boulder"}, {"small_pebble", "cobble"},
      {"scots pine", "conifer"},         {"english oak", "canopy_tree"},
      {"silver birch 03", "canopy_tree"},{"bracken", "fern"},
      {"moss patch", "moss"},            {"meadow grass", "grass"},
      {"foxglove", "flower"},            {"fallen log", "deadwood"},
      {"saguaro", "columnar_cactus"},    {"kelp frond", "kelp"},
  };
  for (const Case &c : CASES)
    check(group_from_name(c.name) == c.group,
          std::string("'") + c.name + "' is a " + c.group + " (got '" + group_from_name(c.name) + "')");
  // and a thing it cannot place says so, rather than guessing
  check(group_from_name("zx_asset_0041").empty(), "a name it cannot read is left for a person");
}

int main() {
  fails = 0;
  test_tables_are_whole();
  test_things_come_after_what_they_need();
  test_the_rules_say_what_they_mean();
  test_biomes_read_as_places();
  test_a_name_finds_its_group();
  if (fails) {
    std::printf("%d ecology check(s) failed\n", fails);
    return 1;
  }
  std::printf("ecology tests passed\n");
  return 0;
}
