// Geekatplay TerraForge - what a node is called on screen.
//
// The type is identity and never changes; the display name is what a person
// reads and is free to. What must hold:
//   - splitting the case handles the ordinary cases;
//   - acronyms survive the split, which the naive rule gets wrong in exactly
//     the way that produced "AOFrom Height" and "PBRMaterial";
//   - an override wins over the split;
//   - every registered node has a name, and none of them is still the raw
//     identifier when the identifier would read badly.
#include "gpx/node_graph.hpp"
#include <cctype>
#include <cstdio>
#include <string>

using namespace gpx;

static int g_fail = 0, g_checks = 0;
static void check(bool ok, const std::string &what) {
  ++g_checks;
  if (!ok) {
    std::printf("  [FAIL] %s\n", what.c_str());
    ++g_fail;
  }
}

static void expect(const char *type, const char *want) {
  const std::string &got = node_display_name(type);
  check(got == want, std::string(type) + " -> \"" + got + "\", wanted \"" +
                         want + "\"");
}

static void test_case_splitting() {
  std::printf("node names: splitting the case...\n");
  expect("FieldColorMix", "Field color mix");
  expect("SelectAltitude", "Select altitude");
  expect("Smooth", "Smooth");
  expect("TerrainOutput", "Terrain output");
  expect("PointsToMask", "Points to mask");
}

static void test_acronyms_survive() {
  std::printf("node names: acronyms...\n");
  // The whole reason this file exists: a naive split cuts inside an acronym
  // and produces "AOFrom Height".
  expect("AOFromHeight", "Ambient occlusion from height"); // override
  expect("PBRMaterial", "PBR material");                   // override
  expect("FieldColorHSV", "Field color HSV");              // split, HSV intact
  expect("FieldColorFromHSV", "Field color from HSV");
  check(node_display_name("FieldColorHSV").find("HSV") != std::string::npos,
        "an acronym stays upper case after the split");
}

static void test_overrides_win() {
  std::printf("node names: overrides...\n");
  expect("TerrainFractal2", "Rock and soil terrain"); // a version is not a name
  expect("PathSDF", "Distance from a path");          // jargon
  expect("FieldStones", "Stone field");
  check(node_display_name("TerrainFractal2").find('2') == std::string::npos,
        "a version suffix does not reach the user");
}

// Every node, not just the ones anyone thought to name.
static void test_every_node_is_named() {
  std::printf("node names: the whole registry...\n");
  int unnamed = 0, still_identifier = 0, bad_case = 0;
  for (const NodeDef *d : NodeRegistry::instance().all()) {
    const std::string &n = node_display_name(d->type);
    if (n.empty()) {
      ++unnamed;
      std::printf("  [FAIL] %s has no display name\n", d->type.c_str());
    }
    // A name identical to the type means the split did nothing, which for a
    // multi-word identifier means it failed.
    if (n == d->type) {
      int caps = 0;
      for (char c : d->type)
        if (std::isupper((unsigned char)c)) ++caps;
      if (caps > 1) {
        ++still_identifier;
        std::printf("  [FAIL] %s still reads as its own identifier\n",
                    d->type.c_str());
      }
    }
    // Sentence case: the first letter up, and no interior word starting with
    // a capital unless it is an acronym (all-caps run).
    if (!n.empty() && !std::isupper((unsigned char)n[0]) &&
        !std::isdigit((unsigned char)n[0]))
      ++bad_case;
  }
  check(unnamed == 0, "every registered node has a display name");
  check(still_identifier == 0,
        "no multi-word type is shown as its raw identifier");
  check(bad_case == 0, "every display name starts with a capital");
}

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("Geekatplay TerraForge - node display names\n\n");
  test_case_splitting();
  test_acronyms_survive();
  test_overrides_win();
  test_every_node_is_named();
  std::printf("\n%d checks, %s\n", g_checks, g_fail ? "FAILED" : "all passed");
  return g_fail ? 1 : 0;
}
