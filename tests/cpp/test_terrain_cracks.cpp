// Geekatplay TerraForge - the crack invariant, before the quadtree.
//
// TF-GEO-0's stop condition is "reject the quadtree if any crack is
// reproducible - a hole is not a performance trade". The spec also says to
// build this test first, and it is right to: what it establishes is not
// whether an implementation is correct but which crack policy can be correct
// at all, which is a question worth answering before writing the shader.
//
// The finding is at the bottom and it is not the one the roadmap assumed.
#include "gpx/tess_edge.hpp"
#include <cmath>
#include <cstdio>
#include <string>

using namespace gpx::tess;

static int failures = 0;
static std::string g_case;

static void check(bool ok, const std::string &what) {
  if (ok) return;
  std::printf("  FAIL [%s] %s\n", g_case.c_str(), what.c_str());
  ++failures;
}

int main() {
  std::printf("Geekatplay TerraForge - terrain crack invariant\n\n");

  // ---- the spacing model matches what the hardware actually did ---------
  //
  // Not a claim: at the shipping floor of 8 the GPU counter reported 162
  // triangles per patch, and 162 is 2 * 9 * 9. If this model said 8 it would
  // be modelling something else.
  {
    g_case = "segments";
    check(segments(8.f, Spacing::FractionalOdd) == 9,
          "level 8 becomes 9 segments, got " +
              std::to_string(segments(8.f, Spacing::FractionalOdd)));
    check(segments(9.f, Spacing::FractionalOdd) == 9, "9 stays 9");
    check(segments(9.1f, Spacing::FractionalOdd) == 11, "9.1 becomes 11");
    check(segments(1.f, Spacing::FractionalOdd) == 1, "1 is a single segment");
    check(segments(0.2f, Spacing::FractionalOdd) == 1, "and so is less");
    check(segments(8.f, Spacing::Equal) == 8, "equal spacing keeps 8");
    check(segments(8.1f, Spacing::Equal) == 9, "and rounds 8.1 up");
    check(segments(1000.f, Spacing::FractionalOdd) <= 65,
          "clamped to the hardware limit");
  }

  // ---- an odd level is uniform, a fractional one is not -----------------
  {
    g_case = "edge vertices";
    auto v = edge_vertices(9.f, Spacing::FractionalOdd);
    check(v.size() == 10, "9 segments is 10 vertices, got " +
                              std::to_string(v.size()));
    float worst = 0.f;
    for (size_t i = 1; i < v.size(); ++i)
      worst = std::max(worst, std::fabs((v[i] - v[i - 1]) - 1.f / 9.f));
    check(worst < 1e-5f, "an exactly-odd level is uniform, worst deviation " +
                             std::to_string(worst));

    // just above 7: the two new vertices are born at the corners, which is
    // the whole character of fractional_odd and the reason it does not pop
    auto w = edge_vertices(7.001f, Spacing::FractionalOdd);
    check(w.size() == 10, "7.001 already has 9 segments");
    check(w[1] < 0.002f, "the second vertex starts at the corner, at " +
                             std::to_string(w[1]));
    check(w[w.size() - 2] > 0.998f, "and so does the second from the end");

    for (float t : {1.5f, 4.f, 7.3f, 12.f, 31.9f, 64.f}) {
      auto e = edge_vertices(t, Spacing::FractionalOdd);
      check(e.front() == 0.f && e.back() == 1.f,
            "level " + std::to_string(t) + " starts at 0 and ends at 1");
      bool monotone = true;
      for (size_t i = 1; i < e.size(); ++i)
        monotone = monotone && e[i] >= e[i - 1];
      check(monotone, "level " + std::to_string(t) + " is monotone");
    }
  }

  // ---- today's grid: same level both sides, so no gap -------------------
  //
  // This is the invariant the current design has and the one a quadtree
  // threatens. It holds because edge_level is a function of the two shared
  // endpoints, so both patches feed the tessellator the identical float.
  {
    g_case = "uniform grid";
    for (float px : {3.f, 17.f, 140.f, 900.f, 4000.f}) {
      const float t = edge_level(px, 8.f, 8.f, 32.f);
      auto a = edge_vertices(t, Spacing::FractionalOdd);
      auto b = edge_vertices(t, Spacing::FractionalOdd);
      check(boundary_gap(a, b) == 0.f,
            "an edge " + std::to_string((int)px) +
                " px long agrees with itself exactly");
    }
  }

  // ---- the tapered floor still agrees across an edge --------------------
  //
  // The floor now depends on the edge's pixel length. That is fine only
  // because both patches measure the same edge; if it ever came to depend on
  // anything belonging to one patch and not the other, this would catch it.
  {
    g_case = "tapered floor";
    for (float px : {0.5f, 2.f, 8.f, 15.9f, 16.1f, 64.f}) {
      const float t1 = edge_level(px, 8.f, 8.f, 32.f);
      const float t2 = edge_level(px, 8.f, 8.f, 32.f);
      check(t1 == t2, "the same edge gives the same level at " +
                          std::to_string(px) + " px");
      check(t1 >= 1.f, "and never below one segment");
    }
    // and the taper only ever lowers the floor, never the metric's own ask
    for (float px = 1.f; px < 400.f; px += 3.f) {
      const float t = edge_level(px, 8.f, 8.f, 32.f);
      check(t >= std::min(8.f, std::max(px * 0.5f, 1.f)) - 1e-5f,
            "the floor is respected at " + std::to_string(px) + " px");
      check(t <= 32.f + 1e-5f, "the cap is respected at " +
                                   std::to_string(px) + " px");
    }
    // The taper is the point, so it has to be asserted and not merely
    // permitted: an edge two pixels long must come out *below* the floor,
    // which is where the 98% at orbital range came from. A flat floor
    // satisfies every bound above and would slip through.
    check(edge_level(2.f, 8.f, 8.f, 32.f) < 2.f,
          "a 2 px edge falls below the floor, got " +
              std::to_string(edge_level(2.f, 8.f, 8.f, 32.f)));
    check(edge_level(0.4f, 8.f, 8.f, 32.f) <= 1.f,
          "a sub-pixel edge asks for a single segment");
    check(edge_level(200.f, 8.f, 8.f, 32.f) == 25.f,
          "and a long edge is untouched by the taper");
  }

  // ---- a T-junction under fractional_odd: this is the finding -----------
  //
  // A coarse patch beside two finer ones shares half an edge with each. The
  // obvious level assignment - every edge asks the screen-space metric about
  // its own endpoints - is what the current design does, and across a
  // T-junction it does not line up. The coarse side's vertices are spread
  // over an edge twice as long, and no amount of agreeing on a rule makes
  // them fall where the fine side's did.
  {
    g_case = "T-junction, fractional odd";
    // the coarse edge is 200 px; each fine half is 100 px
    const float coarse = edge_level(200.f, 8.f, 8.f, 32.f);
    const float fine = edge_level(100.f, 8.f, 8.f, 32.f);
    auto c = edge_vertices(coarse, Spacing::FractionalOdd);
    auto f0 = edge_vertices(fine, Spacing::FractionalOdd);
    const float gap = boundary_gap(c, f0, 0.f, 0.5f);
    check(gap > 0.01f,
          "the obvious assignment leaves a gap of " + std::to_string(gap) +
              " of an edge - if this ever reads zero the finding below has "
              "changed and the quadtree design should be revisited");
    std::printf("      coarse level %.2f vs fine %.2f: worst gap %.4f of the "
                "edge\n", coarse, fine, gap);

    std::printf("      coarse %d segments vs %d + %d across the two halves\n",
                segments(coarse, Spacing::FractionalOdd),
                segments(fine, Spacing::FractionalOdd),
                segments(fine, Spacing::FractionalOdd));

    // Note what the metric already did: the coarse edge is twice as long in
    // pixels, so it asked for twice the level, unprompted. The 2:1 relation
    // a quadtree wants is not something that has to be arranged - it falls
    // out - and the gap is there anyway. So the problem is not the levels.
    check(std::fabs(coarse - fine * 2.f) < 1e-4f,
          "the screen metric already gives the coarse edge twice the level");
  }

  // ---- why, and it is a parity argument ---------------------------------
  //
  // fractional_odd always rounds up to an odd number of segments. Two fine
  // neighbours therefore contribute odd + odd, which is even; the coarse
  // edge beside them contributes odd. An odd number of segments can never
  // equal an even number of them, so the two sides cannot place the same
  // vertices - at any level, at any distance, on any hardware.
  //
  // That is not a bug to find and fix later. It rules out the spacing mode
  // for a quadtree outright, and it is the reason this test was worth
  // writing before the shader rather than after.
  {
    g_case = "the parity argument";
    for (float fine = 1.f; fine <= 32.f; fine += 0.5f) {
      const int nf = segments(fine, Spacing::FractionalOdd);
      const int nc = segments(fine * 2.f, Spacing::FractionalOdd);
      check(nf % 2 == 1, "a fine edge has an odd segment count at level " +
                             std::to_string(fine));
      check(nc % 2 == 1, "and so does the coarse edge above it");
      check(nc != 2 * nf,
            "so the coarse edge cannot match its two neighbours' " +
                std::to_string(2 * nf) + " segments (it has " +
                std::to_string(nc) + ")");
    }
  }

  // ---- and the policy that does work ------------------------------------
  //
  // equal_spacing with the coarse edge at exactly twice the fine level. Then
  // the coarse edge's segments are the same world length as the fine ones,
  // and every fine vertex has a coarse vertex on top of it. This is the
  // constraint a quadtree has to accept, and its cost is popping: an integer
  // level steps, and a stepping subdivision is visible.
  {
    g_case = "T-junction, equal spacing at 2:1";
    for (int fine_n = 1; fine_n <= 16; ++fine_n) {
      auto f0 = edge_vertices((float)fine_n, Spacing::Equal);
      auto f1 = edge_vertices((float)fine_n, Spacing::Equal);
      auto c = edge_vertices((float)(2 * fine_n), Spacing::Equal);
      const float g0 = boundary_gap(c, f0, 0.f, 0.5f);
      const float g1 = boundary_gap(c, f1, 0.5f, 0.5f);
      check(g0 < 1e-6f && g1 < 1e-6f,
            "fine level " + std::to_string(fine_n) +
                " under a coarse level " + std::to_string(2 * fine_n) +
                " leaves no gap (" + std::to_string(std::max(g0, g1)) + ")");
    }
    // and it genuinely needs the 2:1 relation - one off and the hole opens
    auto f = edge_vertices(8.f, Spacing::Equal);
    auto c = edge_vertices(15.f, Spacing::Equal);
    check(boundary_gap(c, f, 0.f, 0.5f) > 1e-4f,
          "a coarse level of 15 against a fine 8 does leave a gap");
  }

  // ---- the commonest crack: a coarse side that is merely too sparse -----
  //
  // Every vertex the coarse side places sits exactly on a fine one, so
  // looking only from the coarse side sees nothing wrong. The hole is the
  // fine side's vertices that the coarse side has no answer for: the ground
  // is displaced there on one side of the seam and flat on the other. This
  // is what a T-junction actually looks like, and a gap measured in one
  // direction cannot see it.
  {
    g_case = "a coarse side with nothing wrong from its own point of view";
    auto coarse = edge_vertices(2.f, Spacing::Equal);   // 0, 0.5, 1
    auto fine = edge_vertices(4.f, Spacing::Equal);     // over [0, 0.5]
    float from_coarse = 0.f;
    for (float x : coarse) {
      if (x > 0.5f + 1e-6f) continue;
      float d = 1e30f;
      for (float y : fine) d = std::min(d, std::fabs(y * 0.5f - x));
      from_coarse = std::max(from_coarse, d);
    }
    check(from_coarse < 1e-6f,
          "every coarse vertex lands on a fine one, so from that side all is "
          "well (" + std::to_string(from_coarse) + ")");
    check(boundary_gap(coarse, fine, 0.f, 0.5f) > 0.1f,
          "but the seam is open, by " +
              std::to_string(boundary_gap(coarse, fine, 0.f, 0.5f)) +
              " of an edge");
  }

  std::printf("\nThe finding: under fractional_odd_spacing a T-junction "
              "cannot be closed\nby choosing levels, because the two vertices "
              "the rule adds are born at the\ncorners and never land where "
              "the finer neighbour's interior vertices are.\nA quadtree needs "
              "equal_spacing with a 2:1 level relation across every\nlevel "
              "boundary - and that trades the popping fractional_odd was "
              "chosen to\navoid. That trade is TF-GEO-0's real design "
              "decision.\n");

  if (failures)
    std::printf("\n%d failure(s)\n", failures);
  else
    std::printf("\nall passed\n");
  return failures ? 1 : 0;
}
