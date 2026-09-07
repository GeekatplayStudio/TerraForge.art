// Geekatplay TerraForge - a field of stones, as a function of position.
//
// A heightmap cannot hold a stone. On a 5 km tile at 1024 a texel is 4.9 m,
// so a boulder is a fifth of a texel and a pebble is nothing at all; the
// raster FakeStones node is a layout generator, not a stone generator, and
// its smallest expressible stone is 14 m across. Terragen's answer, and
// ours, is to stop storing the stones and evaluate them instead: this is a
// function of a point, so it has no resolution, and the GPU calls it per
// vertex and per pixel at whatever density the camera needs.
//
// What makes a field of these read as stones rather than as bumps:
//   - a power-law size spectrum: many small, a few large, as scree is;
//   - a height per stone, not one height for the field (the raster node's
//     most visible tell - every stone the same height, only the footprint
//     varying);
//   - a lean, so the apex is off centre and the stone has a downhill side;
//   - burial, so a stone shows only its top and its outline is cut by the
//     ground rather than meeting it tangentially;
//   - a plateau rather than a scaled dome when flattened, which is what a
//     slab actually is;
//   - stones that differ from one another and not only from the average:
//     rounded cobbles and shattered blocks in the same field;
//   - stones heaped against one another rather than merely more numerous in
//     one place, because a clump of stones is stones that touch;
//   - a number per stone, so a material can shade each one differently -
//     one colour over a whole field is the last tell of a procedural;
//   - octaves: half the cell and half the stone each time, so boulders,
//     cobbles and gravel come out of one function and one cost.
//
// This is the CPU truth. `gpxf_stones` in engine/field_glsl.cpp is a line
// for line mirror of it, and the CPU/GPU agreement check holds the two
// together (studio/field_gpu_check.cpp).
#pragma once
#include "gpx/planet_math.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gpx::stones {

// Every length is in graph units: the terrain tile is 1 across. The node
// converts from metres, so an author sizes stones in metres and the field
// stays resolution-free.
struct Params {
  float cell = 1.2e-4f;    // cell size: one stone's worth of ground
  float density = 0.55f;   // 0..1 coverage target; 1 paves the ground over
  float tallness = 0.6f;   // height as a fraction of the stone's radius
  float flatten = 0.25f;   // 0 boulders .. 1 slabs
  float bury = 0.25f;      // how deep a stone sits in the ground
  float tilt = 0.35f;      // how far a stone leans off vertical
  float spread = 0.7f;     // size spectrum: 0 all alike, 1 many tiny, few large
  float elongation = 0.5f; // 0 round in plan, 1 up to three times as long
  float rough = 0.45f;     // how far the outline departs from an ellipse
  float facet = 0.55f;     // flat broken faces cut into the form
  float bumpy = 0.4f;      // relief across a stone's own surface
  float variation = 0.6f;  // 0 every stone shaped alike .. 1 cobbles to blocks
  float height_var = 0.5f; // spread of heights about the average, 0 all alike
  // How the population is split between the big stones and the small. Each
  // octave's density is multiplied by this again, so it is a geometric
  // weighting across the size range; the node computes it from a -1..1 dial
  // so the shader never runs a pow.
  float size_step = 1.f;
  float cluster = 0.5f;    // -1 evenly spaced .. 0 scattered .. 1 heaped
  float cluster_cells = 14.f; // a drift is this many cells across
  uint32_t seed = 0;
};

// Cheap further 32 bits from one hash. A full pl_hash_bits per stone would
// be another integer hash in the innermost loop of the innermost loop;
// these avalanches are two operations each and are just as uncorrelated for
// choosing a facet's direction or a stone's shade. Two constants, because
// these are chained and one function applied twice is not a new hash.
inline uint32_t remix(uint32_t h) {
  h *= 2654435761u;
  h ^= h >> 15;
  return h;
}
inline uint32_t remix2(uint32_t h) {
  h *= 0x85ebca6bu;
  h ^= h >> 13;
  return h;
}

// Smooth 0..1 noise on the cluster lattice, from four corner hashes, and
// its gradient. Cells ask this whether they are in a drift of stones or on
// bare ground between them, and stones ask which way the middle of their
// drift lies so they can heap up against one another. The gradient is
// analytic because a bilinear patch differentiates to a handful of
// multiplies - a finite difference would be eight more hashes per cell.
//
// It is deliberately a coarse, cheap field: a drift is tens of cells across
// and does not need detail of its own.
inline void cluster_at(float gx, float gz, uint32_t seed, float &v, float &dx,
                       float &dz) {
  const float fx = std::floor(gx), fz = std::floor(gz);
  const int qx = (int)fx, qz = (int)fz;
  const float ux = gx - fx, uz = gz - fz;
  const float ax = ux * ux * (3.f - 2.f * ux), az = uz * uz * (3.f - 2.f * uz);
  const float dax = 6.f * ux * (1.f - ux), daz = 6.f * uz * (1.f - uz);
  const float n00 = (float)(planet::pl_hash_bits(qx, 5, qz, seed) & 0xffffu) * (1.f / 65535.f);
  const float n10 = (float)(planet::pl_hash_bits(qx + 1, 5, qz, seed) & 0xffffu) * (1.f / 65535.f);
  const float n01 = (float)(planet::pl_hash_bits(qx, 5, qz + 1, seed) & 0xffffu) * (1.f / 65535.f);
  const float n11 = (float)(planet::pl_hash_bits(qx + 1, 5, qz + 1, seed) & 0xffffu) * (1.f / 65535.f);
  const float a = n00 + (n10 - n00) * ax;
  const float b = n01 + (n11 - n01) * ax;
  v = a + (b - a) * az;
  dx = ((n10 - n00) * (1.f - az) + (n11 - n01) * az) * dax;
  dz = (b - a) * daz;
}

inline constexpr int MAX_OCTAVES = 6;

// Height (graph units), coverage (0..1) and a per-stone shade (0..1, the
// stone the point actually sits on) at a point on the ground. `oct` is the
// octave budget the caller can afford - the field-domain LOD.
inline void field(const Params &p, float x, float z, int oct, float &height,
                  float &mask, float &shade) {
  float total = 0.f, cover = 0.f, tone = 0.f;
  float cs = std::max(p.cell, 1e-9f);
  const int octaves = std::clamp(oct, 1, MAX_OCTAVES);
  // Density is a coverage target, not a count. Up to three quarters it
  // thins the field; past that every cell holds a stone and the stones
  // grow into one another and the smallest are lifted, so 1 is ground
  // paved end to end with no bare earth left between. The 3x3 window puts
  // the ceiling on that growth: a radius over one cell would let a stone
  // reach in from outside the window and leave a seam, and the outline
  // wobble already spends a quarter of the margin.
  const float fill = std::clamp(p.density, 0.f, 1.f);
  const float chance = std::min(fill * (4.f / 3.f), 1.f);
  const float packt = std::clamp((fill - 0.75f) * 4.f, 0.f, 1.f);
  const float pack = 1.f + 0.5f * packt;
  // Which sizes the field is made of. Every octave's share is multiplied by
  // this again, so it weights the whole size range geometrically: a boulder
  // field with a little gravel, or gravel with the odd boulder in it, out of
  // the same octaves.
  float ow = 1.f;
  for (int o = 0; o < octaves; ++o) {
    const uint32_t oseed = p.seed + (uint32_t)o * 7919u;
    const float inv = 1.f / cs;
    const float fx = x * inv, fz = z * inv;
    const int ix = (int)std::floor(fx), iz = (int)std::floor(fz);
    for (int dz = -1; dz <= 1; ++dz)
      for (int dx = -1; dx <= 1; ++dx) {
        const int cxi = ix + dx, czi = iz + dz;
        const uint32_t h = planet::pl_hash_bits(cxi, 0, czi, oseed);
        // Does this cell hold a stone at all - and stones are not spread
        // evenly. They collect in drifts with bare ground between, so the
        // chance is modulated by a slow field over the cell lattice. The
        // mean is preserved, so raising the clustering rearranges a field
        // without thinning it.
        float local_density = chance * ow;
        float cgx = 0.f, cgz = 0.f;
        if (p.cluster > 0.f) {
          const float inv_cc = 1.f / std::max(p.cluster_cells, 1.f);
          float cn = 0.f;
          cluster_at((float)cxi * inv_cc, (float)czi * inv_cc, oseed ^ 0x5bd1u,
                     cn, cgx, cgz);
          local_density *= 1.f - p.cluster + p.cluster * cn * 2.f;
        }
        const float exist = (float)(h & 0xfffu) * (1.f / 4095.f);
        if (exist > local_density) continue;
        const uint32_t h2 = planet::pl_hash_bits(cxi, 1, czi, oseed);
        // its centre, anywhere in the cell - a stone on the border is as
        // likely as one in the middle, or the lattice stays visible
        float ox = (float)((h >> 12) & 0x3ffu) * (1.f / 1023.f);
        float oz = (float)((h >> 22) & 0x3ffu) * (1.f / 1023.f);
        // Heaped together, or pushed apart. Thinning a field by a drift
        // field says how many stones a patch of ground gets; it does not
        // make them touch, and stones that never touch read as scattered
        // marbles. So the drift's gradient pulls each stone toward the side
        // of its cell the middle of the drift lies on, and stones in a
        // drift end up shoulder to shoulder while the ground between them
        // is bare. It stays a blend of two points inside the cell, so a
        // stone can never leave its own cell and reach in from outside the
        // 3x3 window.
        //
        // Negative clustering is the opposite move: take the jitter out and
        // the stones stand off from one another, which is repulsion - what
        // frost heave and a slope's own sorting actually do to a boulder
        // field.
        if (p.cluster > 0.f) {
          const float tx = 0.5f + 0.5f * std::clamp(cgx * 2.f, -1.f, 1.f);
          const float tz = 0.5f + 0.5f * std::clamp(cgz * 2.f, -1.f, 1.f);
          ox += (tx - ox) * p.cluster * 0.75f;
          oz += (tz - oz) * p.cluster * 0.75f;
        } else if (p.cluster < 0.f) {
          const float k = 1.f + p.cluster * 0.85f; // 1 .. 0.15
          ox = 0.5f + (ox - 0.5f) * k;
          oz = 0.5f + (oz - 0.5f) * k;
        }
        // power-law size: t^3 gives many small and a few large, the
        // spectrum a scree slope actually has
        const float t = (float)(h2 & 0x3ffu) * (1.f / 1023.f);
        float sz = 1.f - p.spread + p.spread * t * t * t;
        sz += (1.f - sz) * 0.5f * packt; // paved ground has no room for grit
        const float rad = 0.5f * sz * pack;
        const uint32_t h3 = planet::pl_hash_bits(cxi, 2, czi, oseed);
        const float ddx = fx - ((float)cxi + ox), ddz = fz - ((float)czi + oz);
        // A stone lies the way it fell: turned, and longer one way than the
        // other. Circles in plan are the tell of a procedural field.
        //
        // No angles anywhere in here. This is the innermost loop of the
        // innermost loop - nine cells an octave, five octaves, and the
        // fragment stage calls the whole thing four times over for the
        // shading normal - so a sine costs more than the stone is worth.
        // A turn is a hashed unit vector, and the harmonics that roughen
        // the outline come from the multiple-angle identities on that same
        // vector, which are multiplies.
        const float vx = (float)(h3 & 0x3ffu) * (2.f / 1023.f) - 1.f;
        const float vz = (float)((h3 >> 10) & 0x3ffu) * (2.f / 1023.f) - 1.f;
        const float vl = std::sqrt(vx * vx + vz * vz);
        const float inv_vl = vl > 1e-6f ? 1.f / vl : 1.f;
        const float cr = vx * inv_vl, sr = vz * inv_vl;
        const float rx = ddx * cr + ddz * sr, rz = -ddx * sr + ddz * cr;
        const float aspect =
            1.f + p.elongation * 2.f * (float)((h3 >> 20) & 0x3ffu) * (1.f / 1023.f);
        const float ex = rx / aspect, ez = rz;
        float rr = std::sqrt(ex * ex + ez * ez);
        // the direction as a unit vector, which is the cosine and sine of
        // the angle we never computed
        const float inv_rr = rr > 1e-9f ? 1.f / rr : 0.f;
        const float c1 = ex * inv_rr, s1 = ez * inv_rr;
        const float c2 = c1 * c1 - s1 * s1, s2 = 2.f * c1 * s1;
        const float c3 = c1 * c2 - s1 * s2, s3 = s1 * c2 + c1 * s2;
        const float c5 = c3 * c2 - s3 * s2, s5 = s3 * c2 + c3 * s2;
        // each harmonic's phase is a hashed unit vector too
        const float q1 = (float)((h3 >> 30) & 0x3u) * (2.f / 3.f) - 1.f;
        const float q2 = (float)((h2 >> 30) & 0x3u) * (2.f / 3.f) - 1.f;
        const float wob = 1.f + p.rough * (0.13f * (s3 * (1.f - std::fabs(q1)) + c3 * q1) +
                                           0.07f * (s5 * (1.f - std::fabs(q2)) + c5 * q2));
        rr /= std::max(wob, 0.2f);
        const float r2 = (rr * rr) / (rad * rad);
        if (r2 >= 1.f) continue;
        const float base = 1.f - r2;
        // the profile: a rounder or a squarer stone, per stone
        const float e = 0.35f + 0.5f * (float)((h2 >> 10) & 0xffu) * (1.f / 255.f);
        float prof = std::pow(base, e);
        // These three only ever run for a stone the point is actually
        // inside, which is why they sit below the footprint test rather
        // than beside the hashes they come from.
        const uint32_t h4 = remix(h2), h5 = remix(h3), h6 = remix2(h4);
        // How much stones differ from one another. Without this every stone
        // in a field is broken, flattened and pitted to exactly the same
        // degree and only its outline is its own, which is the look of a
        // texture rather than of ground; real scree holds rounded cobbles
        // and shattered blocks side by side. The multiplier averages to
        // one, so turning variation up widens the field's range without
        // changing its character - and the two draws are used against each
        // other, so the flat slabs come out smooth and the lumpy stones
        // pitted rather than every stone drifting the same way at once.
        const float vA = (float)(h5 & 0xffu) * (1.f / 255.f);
        const float vB = (float)(h6 & 0xffu) * (1.f / 255.f);
        const float vary = p.variation;
        const float s_flat =
            std::min(p.flatten * (1.f - vary + vary * 2.f * vA), 1.f);
        const float s_facet =
            std::min(p.facet * (1.f - vary + vary * 2.f * vB), 1.f);
        const float s_bumpy =
            std::min(p.bumpy * (1.f - vary + vary * 2.f * (1.f - vA)), 1.f);
        // flattening raises the top into a plateau and keeps the footprint,
        // which is what a slab is; scaling the dome only makes it smaller
        prof = std::min(prof / std::max(1.f - s_flat * 0.85f, 0.15f), 1.f);
        // Facets. A stone is a broken thing, not a bubble: two planes cut
        // flat faces into the dome, each with its own hashed direction and
        // its own distance from the centre. This is what stops a field of
        // these reading as droplets, and it costs two dot products.
        if (p.facet > 0.f) {
          const float ux = ex * inv_rr, uz = ez * inv_rr;
          const float r01 = std::sqrt(std::max(r2, 0.f)); // 0 centre .. 1 rim
          float cut = 1.f;
          for (int k = 0; k < 2; ++k) {
            const uint32_t hk = k == 0 ? h4 : h5;
            const float nx = (float)((hk >> 8) & 0xffu) * (2.f / 255.f) - 1.f;
            const float nz = (float)((hk >> 16) & 0xffu) * (2.f / 255.f) - 1.f;
            const float nl = std::sqrt(nx * nx + nz * nz);
            const float inv_nl = nl > 1e-6f ? 1.f / nl : 1.f;
            const float off = 0.35f + 0.5f * (float)((hk >> 24) & 0xffu) * (1.f / 255.f);
            cut = std::min(cut, off - r01 * (ux * nx + uz * nz) * inv_nl);
          }
          prof *= 1.f - s_facet * (1.f - std::clamp(cut * 2.0f, 0.f, 1.f));
        }
        // and the surface itself is not polished: relief that varies both
        // around the stone and out from its middle
        if (p.bumpy > 0.f)
          prof *= 1.f + s_bumpy * 0.22f *
                            (s3 * (2.f * base - 1.f) + c5 * (1.f - base));
        // the lean: the apex moves off centre, so the stone has a high side
        // the lean's direction: another hashed vector, no angle
        const float lx = (float)((h2 >> 18) & 0x3fu) * (2.f / 63.f) - 1.f;
        const float lz = (float)((h2 >> 24) & 0x3fu) * (2.f / 63.f) - 1.f;
        const float ll = std::sqrt(lx * lx + lz * lz);
        const float inv_ll = ll > 1e-6f ? 1.f / ll : 1.f;
        const float lean = (ex * lx * inv_ll + ez * lz * inv_ll) / rad;
        prof += p.tilt * lean * base * 0.5f;
        // this stone's own height (its own bits: the old ones overlapped
        // the lean's, so tall stones leaned the same way)
        const float hv = (float)(h4 & 0x3fu) * (1.f / 63.f);
        // The spread of heights about the average. 0.5 reproduces the old
        // fixed 0.6..1.4, which is why it is the default: the mean is 1
        // whatever this is set to, so widening the spread does not quietly
        // raise or lower the whole field.
        const float tall = 1.f + p.height_var * (hv - 0.5f) * 1.6f;
        const float H = rad * cs * p.tallness * tall;
        const float hs = H * prof - p.bury * H;
        if (hs <= 0.f) continue;
        // stones meet in a crease rather than blending into one another
        if (hs > total) {
          total = hs;
          // Whose stone this is. A field of one colour is the last thing
          // that gives a procedural stone away, so the winning stone hands
          // out its own number and a material can shade every stone
          // differently from its neighbour.
          tone = (float)((h6 >> 8) & 0xffffu) * (1.f / 65535.f);
        }
        cover = std::max(cover, std::min(base * 3.f, 1.f));
      }
    cs *= 0.5f;
    ow *= p.size_step;
  }
  height = total;
  mask = cover;
  shade = tone;
}

} // namespace gpx::stones
