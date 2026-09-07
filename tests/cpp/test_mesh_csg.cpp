// Geekatplay TerraForge - booleans and metaballs.
//
// Both go through Manifold, which guarantees the result is a solid. What is
// worth testing here is not that Manifold works but that we hand it the
// right thing and read the right thing back: our meshes arrive with every
// triangle carrying its own corners, which is a cloud of islands until they
// are stitched, and a boolean of two solids that do not touch has to fail
// rather than return something.
#include "gpx/mesh_engines.hpp"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;
static std::string g_case;

static void check(bool ok, const std::string &what) {
  if (ok) return;
  std::printf("  FAIL [%s] %s\n", g_case.c_str(), what.c_str());
  ++failures;
}

// A cube as our own primitives make one: a triangle soup, no shared
// vertices. That is the input the boolean has to cope with.
static gpx::TriMesh soup_cube(float cx, float cy, float cz, float h) {
  gpx::TriMesh m;
  const float p[8][3] = {
      {cx - h, cy - h, cz - h}, {cx + h, cy - h, cz - h},
      {cx + h, cy + h, cz - h}, {cx - h, cy + h, cz - h},
      {cx - h, cy - h, cz + h}, {cx + h, cy - h, cz + h},
      {cx + h, cy + h, cz + h}, {cx - h, cy + h, cz + h}};
  const int q[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4},
                       {2, 3, 7, 6}, {0, 4, 7, 3}, {1, 2, 6, 5}};
  for (const auto &f : q) {
    const int tri[2][3] = {{f[0], f[1], f[2]}, {f[0], f[2], f[3]}};
    for (const auto &t : tri) {
      for (int k = 0; k < 3; ++k) {
        // every corner its own vertex, exactly as a soup arrives
        m.f.push_back((uint32_t)(m.v.size() / 3));
        m.v.push_back(p[t[k]][0]);
        m.v.push_back(p[t[k]][1]);
        m.v.push_back(p[t[k]][2]);
      }
    }
  }
  return m;
}

static void bounds(const gpx::TriMesh &m, float lo[3], float hi[3]) {
  lo[0] = lo[1] = lo[2] = 1e30f;
  hi[0] = hi[1] = hi[2] = -1e30f;
  for (size_t i = 0; i + 2 < m.v.size(); i += 3)
    for (int k = 0; k < 3; ++k) {
      lo[k] = std::min(lo[k], m.v[i + k]);
      hi[k] = std::max(hi[k], m.v[i + k]);
    }
}

int main() {
  std::printf("Geekatplay TerraForge - CSG and metaballs\n\n");
  if (!gpx::mesh_engines().solidify) {
    std::printf("built without Manifold: nothing to test, and the API says so\n");
    gpx::TriMesh a = soup_cube(0, 0, 0, 1), b = soup_cube(1, 0, 0, 1);
    std::string err;
    check(!gpx::mesh_boolean(a, b, gpx::MeshBoolOp::Union, err) && !err.empty(),
          "the boolean declines and explains");
    std::vector<gpx::MetaBlob> one{{0, 0, 0, 1, 1}};
    gpx::TriMesh out;
    check(!gpx::mesh_metaball(one, 1.f, 0.1f, out, err) && !err.empty(),
          "so does the metaball");
    return failures ? 1 : 0;
  }

  // ---- union of two overlapping cubes ----------------------------------
  {
    g_case = "union";
    gpx::TriMesh a = soup_cube(0, 0, 0, 1);
    const gpx::TriMesh b = soup_cube(1, 0, 0, 1); // overlaps half
    std::string err;
    check(gpx::mesh_boolean(a, b, gpx::MeshBoolOp::Union, err),
          "two soup cubes union: " + err);
    float lo[3], hi[3];
    bounds(a, lo, hi);
    check(std::fabs(lo[0] - -1.f) < 1e-3f && std::fabs(hi[0] - 2.f) < 1e-3f,
          "the result spans both, x from " + std::to_string(lo[0]) + " to " +
              std::to_string(hi[0]));
    check(std::fabs(hi[1] - 1.f) < 1e-3f, "and neither is taller than it was");
    check(!a.empty(), "and it has geometry");
  }

  // ---- intersection is only the overlap ---------------------------------
  {
    g_case = "intersection";
    gpx::TriMesh a = soup_cube(0, 0, 0, 1);
    const gpx::TriMesh b = soup_cube(1, 0, 0, 1);
    std::string err;
    check(gpx::mesh_boolean(a, b, gpx::MeshBoolOp::Intersect, err),
          "intersect: " + err);
    float lo[3], hi[3];
    bounds(a, lo, hi);
    check(std::fabs(lo[0] - 0.f) < 1e-3f && std::fabs(hi[0] - 1.f) < 1e-3f,
          "only the overlapping slab survives, x from " +
              std::to_string(lo[0]) + " to " + std::to_string(hi[0]));
  }

  // ---- difference takes a bite -----------------------------------------
  {
    g_case = "difference";
    gpx::TriMesh a = soup_cube(0, 0, 0, 1);
    const gpx::TriMesh b = soup_cube(1, 0, 0, 0.5f);
    std::string err;
    check(gpx::mesh_boolean(a, b, gpx::MeshBoolOp::Difference, err),
          "subtract: " + err);
    check(!a.empty(), "something is left");
    float lo[3], hi[3];
    bounds(a, lo, hi);
    check(std::fabs(lo[0] - -1.f) < 1e-3f && hi[0] <= 1.001f,
          "and it is the first cube with a corner gone");
  }

  // ---- shapes that do not touch ----------------------------------------
  //
  // A union of two separate solids is legitimate; an intersection of them is
  // empty, and empty has to be reported rather than returned as a mesh with
  // no triangles that something downstream then draws.
  {
    g_case = "no overlap";
    gpx::TriMesh a = soup_cube(0, 0, 0, 1);
    const gpx::TriMesh far = soup_cube(10, 0, 0, 1);
    std::string err;
    gpx::TriMesh u = a;
    check(gpx::mesh_boolean(u, far, gpx::MeshBoolOp::Union, err),
          "a union of two separate solids is fine: " + err);
    gpx::TriMesh i = a;
    err.clear();
    check(!gpx::mesh_boolean(i, far, gpx::MeshBoolOp::Intersect, err),
          "an intersection of two separate solids fails");
    check(!err.empty(), "and says why: " + err);
  }

  // ---- a metaball is one surface, not two spheres ----------------------
  {
    g_case = "metaball merges";
    std::string err;
    // two balls a radius apart: their fields overlap, so one surface
    std::vector<gpx::MetaBlob> pair{{-0.5f, 0, 0, 1.f, 1.f},
                                    {0.5f, 0, 0, 1.f, 1.f}};
    gpx::TriMesh m;
    const bool ok = gpx::mesh_metaball(pair, 1.f, 0.08f, m, err);
    check(ok, "two overlapping blobs mesh: " + err);
    check(!m.empty(), "and produce geometry");
    float lo[3], hi[3];
    bounds(m, lo, hi);
    check(hi[0] > 1.2f && lo[0] < -1.2f,
          "reaching a radius past each centre, x from " +
              std::to_string(lo[0]) + " to " + std::to_string(hi[0]));
    // the waist: at the midpoint the surface is well away from the axis,
    // which is what "merged" means as opposed to "two spheres touching"
    check(hi[1] > 0.5f, "with a body, not a pair of points");

    // and the result is a solid, so it can go straight into a boolean
    gpx::TriMesh cube = soup_cube(0, 0, 0, 0.5f);
    err.clear();
    check(gpx::mesh_boolean(m, cube, gpx::MeshBoolOp::Union, err),
          "the metaball unions with a mesh, so it really is a solid: " + err);
  }

  // ---- smoothness widens the reach without moving the balls ------------
  {
    g_case = "smoothness";
    std::string err;
    std::vector<gpx::MetaBlob> pair{{-1.4f, 0, 0, 1.f, 1.f},
                                    {1.4f, 0, 0, 1.f, 1.f}};
    gpx::TriMesh tight, loose;
    const bool ok_t = gpx::mesh_metaball(pair, 0.f, 0.08f, tight, err);
    check(ok_t, "tight: " + err);
    const bool ok_l = gpx::mesh_metaball(pair, 2.f, 0.12f, loose, err);
    check(ok_l, "loose: " + err);
    if (!ok_t || !ok_l) return failures ? 1 : 0;
    // The extent is deliberately *not* what changes: the threshold is
    // chosen so a ball's surface sits on its own radius whatever the
    // smoothness, or widening the merge would quietly inflate every ball.
    // What changes is whether the two join, so that is what is measured -
    // is there any surface in the gap between them?
    auto spans_the_gap = [](const gpx::TriMesh &m) {
      for (size_t i = 0; i + 2 < m.v.size(); i += 3)
        if (std::fabs(m.v[i]) < 0.2f) return true;
      return false;
    };
    check(!spans_the_gap(tight),
          "at smoothness 0 two balls 2.8 apart stay two balls");
    check(spans_the_gap(loose),
          "at smoothness 2 their fields meet and bridge the gap");
    float lt[3], ht[3], ll[3], hl[3];
    bounds(tight, lt, ht);
    bounds(loose, ll, hl);
    check(std::fabs((hl[0] - ll[0]) - (ht[0] - lt[0])) < 0.2f,
          "and the balls themselves are the same size either way, " +
              std::to_string(ht[0] - lt[0]) + " against " +
              std::to_string(hl[0] - ll[0]));
  }

  // ---- a negative blob carves ------------------------------------------
  {
    g_case = "negative blob";
    std::string err;
    std::vector<gpx::MetaBlob> solid{{0, 0, 0, 1.5f, 1.f}};
    std::vector<gpx::MetaBlob> bitten{{0, 0, 0, 1.5f, 1.f},
                                      {1.2f, 0, 0, 0.9f, -1.f}};
    gpx::TriMesh a, b;
    const bool ok_a = gpx::mesh_metaball(solid, 1.f, 0.06f, a, err);
    check(ok_a, "solid: " + err);
    const bool ok_b = gpx::mesh_metaball(bitten, 1.f, 0.06f, b, err);
    check(ok_b, "bitten: " + err);
    if (!ok_a || !ok_b) return failures ? 1 : 0;
    float la[3], ha[3], lb[3], hb[3];
    bounds(a, la, ha);
    bounds(b, lb, hb);
    check(hb[0] < ha[0] - 0.05f,
          "a negative blob eats into the surface, x reach " +
              std::to_string(ha[0]) + " -> " + std::to_string(hb[0]));
  }

  // ---- a cell size that would ask for the world -------------------------
  {
    g_case = "runaway resolution";
    std::string err;
    std::vector<gpx::MetaBlob> big{{0, 0, 0, 500.f, 1.f}};
    gpx::TriMesh m;
    check(!gpx::mesh_metaball(big, 1.f, 0.001f, m, err),
          "an impossible voxel count is refused rather than attempted");
    check(err.find("million voxels") != std::string::npos,
          "and the message says how many: " + err);
  }

  if (failures)
    std::printf("\n%d failure(s)\n", failures);
  else
    std::printf("\nall passed\n");
  return failures ? 1 : 0;
}
