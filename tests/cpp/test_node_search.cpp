// Geekatplay TerraForge - finding a node by describing it.
//
// The claim this file has to hold up: a person who knows what they want but
// not what it is called can find the node, and the assistant turning a
// sentence into a graph gets the same answer. Concretely -
//
//   - a word that is in no node's text still finds the right nodes, through
//     the concept table ("rocks" -> the stone nodes);
//   - an exact match outranks an associated one, always;
//   - the query is a phrase, not a keyword ("wear down the mountains");
//   - the ranking is deterministic, because a search that reorders between
//     runs cannot be tested or trusted;
//   - nothing matches when nothing should.
#include "gpx/node_search.hpp"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace gpx;

static int g_fail = 0, g_checks = 0;
static void check(bool ok, const std::string &what) {
  ++g_checks;
  if (!ok) {
    std::printf("  [FAIL] %s\n", what.c_str());
    ++g_fail;
  }
}

static std::vector<std::string> types(const std::vector<search::Hit> &h) {
  std::vector<std::string> t;
  for (const search::Hit &x : h) t.push_back(x.type);
  return t;
}

static bool has(const std::vector<search::Hit> &h, const std::string &type) {
  return std::find_if(h.begin(), h.end(), [&](const search::Hit &x) {
           return x.type == type;
         }) != h.end();
}

static int rank_of(const std::vector<search::Hit> &h, const std::string &type) {
  for (size_t i = 0; i < h.size(); ++i)
    if (h[i].type == type) return (int)i;
  return -1;
}

static void show(const char *q, const std::vector<search::Hit> &h) {
  std::printf("    \"%s\" -> ", q);
  for (size_t i = 0; i < h.size() && i < 5; ++i)
    std::printf("%s%s(%.2f)", i ? ", " : "", h[i].label.c_str(), h[i].score);
  std::printf("\n");
}

// The example from the request: searching for rocks must also show stones.
static void test_concept_reach() {
  std::printf("search: a word the catalogue never uses...\n");
  auto h = search::find_nodes("rocks");
  show("rocks", h);
  check(!h.empty(), "\"rocks\" finds something");
  check(has(h, "FieldStones") || has(h, "FakeStones"),
        "\"rocks\" reaches the stone nodes, which never say \"rock\"");

  auto g = search::find_nodes("gravel");
  show("gravel", g);
  check(has(g, "FieldStones") || has(g, "FakeStones"),
        "\"gravel\" reaches them too");

  auto w = search::find_nodes("wear down the mountains");
  show("wear down the mountains", w);
  check(has(w, "Hydraulic") || has(w, "Thermal") || has(w, "StreamPower"),
        "a phrase about wearing mountains down finds an erosion node");

  auto l = search::find_nodes("standing water");
  show("standing water", l);
  check(has(l, "Flood") || has(l, "Lake"),
        "\"standing water\" finds the water-placing nodes");
}

// An exact hit must beat an associated one, or the concept table has made
// search worse rather than better.
static void test_exact_beats_associated() {
  std::printf("search: exact before associated...\n");
  auto h = search::find_nodes("stones");
  show("stones", h);
  const int stone = std::max(rank_of(h, "FieldStones"), rank_of(h, "FakeStones"));
  check(stone >= 0, "the stone nodes are in the results");
  if (stone >= 0) {
    // whichever stone node ranks best must be at or very near the top
    const int best =
        std::min(rank_of(h, "FieldStones") < 0 ? 99 : rank_of(h, "FieldStones"),
                 rank_of(h, "FakeStones") < 0 ? 99 : rank_of(h, "FakeStones"));
    check(best <= 2, "a stone node is in the top three for \"stones\", not "
                     "merely somewhere in the list");
  }

  auto e = search::find_nodes("erosion");
  show("erosion", e);
  check(!e.empty() && e[0].score > 0.f, "\"erosion\" ranks something first");
}

static void test_phrases_and_labels() {
  std::printf("search: phrases and names...\n");
  // The display name is searchable, which is the whole point of having one.
  auto h = search::find_nodes("river incision");
  show("river incision", h);
  check(has(h, "StreamPower"),
        "the node labelled \"River incision\" is found by its label, though "
        "its type says StreamPower");

  auto a = search::find_nodes("ambient occlusion");
  show("ambient occlusion", a);
  check(has(a, "AOFromHeight"),
        "\"ambient occlusion\" finds AOFromHeight by its label");
}

static void test_nothing_matches_nothing() {
  std::printf("search: the empty cases...\n");
  check(search::find_nodes("").empty(), "an empty query returns nothing");
  check(search::find_nodes("the and of").empty(),
        "a query of nothing but stop words returns nothing");
  auto h = search::find_nodes("zzzzqqqx");
  check(h.empty(), "a word in no node and no concept returns nothing");
}

static void test_deterministic() {
  std::printf("search: the same answer twice...\n");
  const char *queries[] = {"rocks", "water erosion", "colour", "clouds"};
  for (const char *q : queries) {
    auto a = types(search::find_nodes(q));
    search::reset_index(); // rebuild from scratch
    auto b = types(search::find_nodes(q));
    check(a == b, std::string("\"") + q +
                      "\" ranks identically after a rebuild - no hash order");
  }
}

static void test_expansion_is_visible() {
  std::printf("search: what it searched for...\n");
  auto w = search::expand_query("rocks");
  check(w.size() > 3, "\"rocks\" expands to several words");
  check(std::find(w.begin(), w.end(), "stone") != w.end(),
        "and \"stone\" is one of them, so the UI can say so");
  // Scoring works on stems, but a stem is not a word. Telling the user it
  // "also searched: ston, talu, lak" is worse than telling them nothing.
  for (const std::string &x : w)
    check(x != "ston" && x != "talu" && x != "lak" && x != "dun",
          "\"" + x + "\" is a real word, not a stem");
}

// A point cloud is a cloud only to a programmer. Putting "cloud" in the
// points row made "clouds at sunset" return the scatter nodes.
static void test_cloud_is_not_a_point_cloud() {
  std::printf("search: cloud means the one in the sky...\n");
  auto h = search::find_nodes("clouds at sunset", 6);
  show("clouds at sunset", h);
  check(!h.empty() && h[0].type.find("Cloud") != std::string::npos,
        "a sky node ranks first for \"clouds\"");
  for (const auto &x : h)
    check(x.type.rfind("Points", 0) != 0 && x.type != "ScatterPoints",
          "\"clouds\" does not reach " + x.type);
}

// Reaching for a brush.
//
// "paint" used to find the mask painter and rank the node that actually
// paints terrain height fourth, at 0.04 - below Snow. "draw" found nothing
// relevant at all. Someone reaching for a brush does not care whether the
// thing they are about to paint is called a sculpt, a mask or a layer, so
// every word for the act has to reach both.
static void test_hand_painting_words() {
  std::printf("search: reaching for a brush...\n");
  for (const char *q : {"paint", "draw", "brush", "sculpt", "paint on terrain"}) {
    auto h = search::find_nodes(q);
    show(q, h);
    check(has(h, "TerrainSculpt"),
          std::string("\"") + q + "\" finds the terrain painter");
    check(has(h, "MaskPaint"),
          std::string("\"") + q + "\" finds the mask painter");
    // and near the top, not buried under whatever happens to share a word
    check(rank_of(h, "TerrainSculpt") < 3,
          std::string("\"") + q + "\" ranks the terrain painter in the top three");
  }
}


int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("Geekatplay TerraForge - node search\n\n");
  test_concept_reach();
  test_exact_beats_associated();
  test_phrases_and_labels();
  test_nothing_matches_nothing();
  test_deterministic();
  test_expansion_is_visible();
  test_cloud_is_not_a_point_cloud();
  test_hand_painting_words();
  std::printf("\n%d checks, %s\n", g_checks, g_fail ? "FAILED" : "all passed");
  return g_fail ? 1 : 0;
}
