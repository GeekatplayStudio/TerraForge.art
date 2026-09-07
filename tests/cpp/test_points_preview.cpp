// Geekatplay TerraForge - point and path nodes draw something.
//
// The unit test next door (test_points_thumb.cpp) proves the picture is right
// for a cloud handed to it. This proves the other half: that these nodes,
// wired the way somebody actually wires them, hand it a cloud at all. Between
// the two, "the node shows what it does" is a claim with evidence rather than
// a claim somebody made once while looking at the screen.
//
// Until this batch these fifteen nodes drew nothing at all - the card carried
// a name and a row of sliders, and whether a scatter clumped had to be
// discovered by rendering the terrain.
#include "gpx/node_graph.hpp"
#include "gpx/points_thumb.hpp"
#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;
static std::string g_node;

static void check(bool ok, const std::string &what) {
  if (ok) return;
  std::printf("  FAIL [%s] %s\n", g_node.c_str(), what.c_str());
  ++failures;
}

using namespace gpx;

// What an unconnected node cannot be blamed for. PathFind routes over a
// heightmap it has not been given; PointsFromCsv reads a file that does not
// exist. Feeding those properly is a different test's job - what matters here
// is that they are named, so a node that quietly stops producing points is
// not mistaken for one of them.
static bool needs_more_than_a_cloud(const std::string &t) {
  return t == "PathFind" || t == "PointsFromCsv";
}

static int lit_pixels(const std::vector<uint8_t> &px) {
  int n = 0;
  for (size_t i = 0; i < px.size(); i += 4)
    if (px[i] > 40) ++n;
  return n;
}

int main() {
  std::printf("Geekatplay TerraForge - point and path node previews\n\n");
  int drawn = 0, exempt = 0;

  for (const NodeDef *d : NodeRegistry::instance().all()) {
    {
      Graph probe;
      probe.resolution = 64;
      Node *t = probe.add_node(d->type);
      if (!t || !t->first_out(DataType::Points)) continue;
    }
    g_node = d->type;

    Graph g;
    g.resolution = 64;
    Node *src = g.add_node("ScatterPoints");
    Node *n = g.add_node(d->type);
    if (!src || !n) {
      check(false, "could not be instantiated");
      continue;
    }
    int wired = 0;
    for (Port &p : n->ports)
      if (p.dir == PortDir::In && p.type == DataType::Points)
        if (g.add_link(src->id, "points", n->id, p.name)) ++wired;
    g.evaluate();

    Port *out = n->first_out(DataType::Points);
    check(out != nullptr, "still has its points output after evaluating");
    if (!out) continue;

    if (needs_more_than_a_cloud(d->type)) {
      ++exempt;
      continue;
    }
    check(n->error.empty(),
          "evaluates without error (got: " + n->error + ")");
    size_t count = out->pts ? out->pts->size() : 0;
    check(count > 0, wired ? "produces points from the scatter it was given"
                           : "produces points on its own");
    if (!count) continue;

    auto px = points_thumbnail(*out->pts, 112, out->name == "path");
    int lit = lit_pixels(px);
    check(lit > 0, "and studio/preview.cpp can draw them - the card is not "
                   "blank");
    // Not a wash either: a thumbnail where every pixel is lit says nothing
    // about where the points are. Dense scatters legitimately cover the tile,
    // so the bar is only that it is a picture and not a solid rectangle at
    // full brightness.
    int full = 0;
    for (size_t i = 0; i < px.size(); i += 4)
      if (px[i] >= 250) ++full;
    check(full < 112 * 112, "and it is a picture, not a solid block");
    ++drawn;
  }

  std::printf("\n%d point/path nodes draw a preview, %d need more than a "
              "cloud to try\n",
              drawn, exempt);
  if (failures)
    std::printf("%d failure(s)\n", failures);
  else
    std::printf("all passed\n");
  return failures ? 1 : 0;
}
